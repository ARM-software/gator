/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#define BUFFER_USE_SESSION_DATA

#include "ExternalSource.h"

#include "Buffer.h"
#include "BufferUtils.h"
#include "Child.h"
#include "CommitTimeChecker.h"
#include "Drivers.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "PrimarySourceProvider.h"
#include "SessionData.h"
#include "Source.h"
#include "lib/AutoClosingFd.h"
#include "lib/FileDescriptor.h"
#include "lib/Memory.h"

#include <atomic>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char STREAMLINE_ANNOTATE[] = "\0streamline-annotate";
static const char MALI_GRAPHICS_STARTUP[] = "\0mali_thirdparty_client";
static const char MALI_GRAPHICS_V1[] = "MALI_GRAPHICS 1\n";
static const char MALI_UTGARD_STARTUP[] = "\0mali-utgard-startup";
static const char FTRACE_V1[] = "FTRACE 1\n";
static const char FTRACE_V2[] = "FTRACE 2\n";

static constexpr int BUFFER_SIZE = 1 * 1024 * 1024;

class ExternalSource : public Source {
public:
    ExternalSource(sem_t & senderSem, Drivers & mDrivers, std::function<uint64_t()> getMonotonicTime)
        : mBufferSem(),
          mGetMonotonicTime(std::move(getMonotonicTime)),
          mCommitChecker(gSessionData.mLiveRate),
          mBuffer(BUFFER_SIZE, senderSem),
          mMonitor(),
          mMidgardStartupUds(MALI_GRAPHICS_STARTUP, sizeof(MALI_GRAPHICS_STARTUP)),
          mUtgardStartupUds(MALI_UTGARD_STARTUP, sizeof(MALI_UTGARD_STARTUP)),
#ifdef TCP_ANNOTATIONS
          mAnnotate(8083),
#endif
          mAnnotateUds(STREAMLINE_ANNOTATE, sizeof(STREAMLINE_ANNOTATE), true),
          mMidgardUds(-1),
          mDrivers(mDrivers)
    {
        sem_init(&mBufferSem, 0, 0);
    }

    void waitFor(const int bytes, const std::function<void()> & endSession)
    {
        while (mBuffer.bytesAvailable() <= bytes) {
            if (gSessionData.mOneShot && mSessionIsActive) {
                logg.logMessage("One shot (external)");
                endSession();
            }
            sem_wait(&mBufferSem);
        }
    }

    void configureConnection(const int fd, const char * const handshake, size_t size)
    {
        if (!lib::setNonblock(fd)) {
            logg.logError("Unable to set nonblock on fh");
            handleException();
        }

        if (!mMonitor.add(fd)) {
            logg.logError("Unable to add fh to monitor");
            handleException();
        }

        // Write the handshake to the circular buffer
        waitFor(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + buffer_utils::MAXSIZE_PACK32 + size - 1, []() {
            logg.logError("Unable to configure connection, buffer too small");
            handleException();
        });
        mBuffer.beginFrame(FrameType::EXTERNAL);
        mBuffer.packInt(fd);
        mBuffer.writeBytes(handshake, size - 1);
        mBuffer.endFrame();
        mBuffer.flush();
    }

    bool connectMidgard()
    {
        mMidgardUds = OlySocket::connect(MALI_GRAPHICS, MALI_GRAPHICS_SIZE);
        if (mMidgardUds < 0) {
            return false;
        }

        if (!mDrivers.getMidgard().start(mMidgardUds)) {
            return false;
        }

        configureConnection(mMidgardUds, MALI_GRAPHICS_V1, sizeof(MALI_GRAPHICS_V1));

        return true;
    }

    void connectFtrace()
    {
        if (!mDrivers.getFtraceDriver().isSupported()) {
            return;
        }

        const std::pair<std::vector<int>, bool> ftraceFds = mDrivers.getFtraceDriver().prepare();
        const char * handshake;
        size_t size;
        if (ftraceFds.second) {
            handshake = FTRACE_V1;
            size = sizeof(FTRACE_V1);
        }
        else {
            handshake = FTRACE_V2;
            size = sizeof(FTRACE_V2);
        }

        for (int fd : ftraceFds.first) {
            configureConnection(fd, handshake, size);
        }
    }

    bool prepare()
    {
        if (!mMonitor.init() || !lib::setNonblock(mMidgardStartupUds.getFd()) ||
            !mMonitor.add(mMidgardStartupUds.getFd()) || !lib::setNonblock(mUtgardStartupUds.getFd()) ||
            !mMonitor.add(mUtgardStartupUds.getFd())
#ifdef TCP_ANNOTATIONS
            || !lib::setNonblock(mAnnotate.getFd()) || !mMonitor.add(mAnnotate.getFd())
#endif
            || !lib::setNonblock(mAnnotateUds.getFd()) || !mMonitor.add(mAnnotateUds.getFd())) {
            return false;
        }

        int pipefd[2];
        if (lib::pipe_cloexec(pipefd) != 0) {
            logg.logError("pipe failed");
            return false;
        }
        mInterruptWrite = pipefd[1];
        mInterruptRead = pipefd[0];

        if (!mMonitor.add(pipefd[0])) {
            logg.logError("Monitor::add failed");
            return false;
        }

        connectMidgard();
        connectFtrace();
        mDrivers.getExternalDriver().start();

        return true;
    }

    void run(std::uint64_t monotonicStart, std::function<void()> endSession) override
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-external"), 0, 0, 0);

        // Gator runs at a high priority, reset the priority to the default
        if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
            logg.logError("setpriority failed");
            handleException();
        }

        // Notify annotate clients to retry connecting to gatord
        uint64_t val = 1;
        if (::write(gSessionData.mAnnotateStart, &val, sizeof(val)) != sizeof(val)) {
            logg.logMessage("Writing to annotate pipe failed");
        }

        if (mDrivers.getFtraceDriver().isSupported()) {
            mDrivers.getAtraceDriver().start();
            mDrivers.getTtraceDriver().start();
            mDrivers.getFtraceDriver().start();
        }

        while (mSessionIsActive) {
            struct epoll_event events[16];
            // Clear any pending sem posts
            while (sem_trywait(&mBufferSem) == 0) {
            }
            int ready = mMonitor.wait(events, ARRAY_LENGTH(events), -1);
            if (ready < 0) {
                logg.logError("Monitor::wait failed");
                handleException();
            }

            for (int i = 0; i < ready; ++i) {
                const int fd = events[i].data.fd;
                if (fd == mMidgardStartupUds.getFd()) {
                    // Midgard says it's alive
                    int client = mMidgardStartupUds.acceptConnection();
                    // Don't read from this connection, establish a new connection to Midgard
                    close(client);
                    if (!connectMidgard()) {
                        logg.logError("Unable to configure incoming Midgard graphics connection");
                        handleException();
                    }
                }
                else if (fd == mUtgardStartupUds.getFd()) {
                    // Mali Utgard says it's alive
                    int client = mUtgardStartupUds.acceptConnection();
                    // Don't read from this connection, configure utgard and expect them to reconnect with annotations
                    close(client);
                    mDrivers.getExternalDriver().disconnect();
                    mDrivers.getExternalDriver().start();
                }
#ifdef TCP_ANNOTATIONS
                else if (fd == mAnnotate.getFd()) {
                    int client = mAnnotate.acceptConnection();
                    if (!lib::setNonblock(client) || !mMonitor.add(client)) {
                        logg.logError("Unable to set socket options on incoming annotation connection");
                        handleException();
                    }
                }
#endif
                else if (fd == mAnnotateUds.getFd()) {
                    int client = mAnnotateUds.acceptConnection();
                    if (!lib::setNonblock(client) || !mMonitor.add(client)) {
                        logg.logError("Unable to set socket options on incoming annotation connection");
                        handleException();
                    }
                }
                else if (fd == *mInterruptRead) {
                    // Means interrupt has been called and mSessionIsActive should be reread
                }
                else {
                    /* This can result in some starvation if there are multiple
                     * threads which are annotating heavily, but it is not
                     * recommended that threads annotate that much as it can also
                     * starve out the gator data.
                     */
                    while (mSessionIsActive) {
                        if (!transfer(monotonicStart, fd, endSession)) {
                            break;
                        }
                    }
                }
            }
        }

        if (mDrivers.getFtraceDriver().isSupported()) {
            const auto ftraceFds = mDrivers.getFtraceDriver().stop();
            // Read any slop
            for (int fd : ftraceFds) {
                transfer(monotonicStart, fd, endSession);
                close(fd);
            }
            mDrivers.getTtraceDriver().stop();
            mDrivers.getAtraceDriver().stop();
        }

        mBuffer.flush();
        mBuffer.setDone();
    }

    bool transfer(const std::uint64_t monotonicStart, const int fd, const std::function<void()> & endSession)
    {
        // Wait until there is enough room for a header and two ints
        waitFor(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + 2 * buffer_utils::MAXSIZE_PACK32, endSession);
        mBuffer.beginFrame(FrameType::EXTERNAL);
        mBuffer.packInt(fd);
        const int contiguous = mBuffer.contiguousSpaceAvailable();
        const int bytes = read(fd, mBuffer.getWritePos(), contiguous);
        if (bytes <= 0) {
            mBuffer.abortFrame();
            if ((bytes < 0) && (errno == EAGAIN)) {
                // Nothing left to read
                return false;
            }
            // if bytes == 0 ; then the other side is closed
            // else something else failed, close the socket
            mBuffer.beginFrame(FrameType::EXTERNAL);
            mBuffer.packInt(-1);
            mBuffer.packInt(fd);
            mBuffer.endFrame();
            // Always force-flush the buffer as this frame don't work like others
            checkFlush(monotonicStart, true);
            close(fd);
            return false;
        }

        mBuffer.advanceWrite(bytes);
        mBuffer.endFrame();
        checkFlush(monotonicStart, isBufferOverFull(mBuffer.contiguousSpaceAvailable()));

        // Short reads also mean nothing is left to read
        return bytes >= contiguous;
    }

    void interrupt() override
    {
        mSessionIsActive = false; // must set this before notifying
        int8_t c = 0;
        // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
        if (::write(*mInterruptWrite, &c, sizeof(c)) != sizeof(c)) {
            logg.logError("write failed");
            handleException();
        }
    }

    bool write(ISender & sender) override
    {
        const bool isDone = mBuffer.write(sender);
        sem_post(&mBufferSem);

        return isDone;
    }

private:
    sem_t mBufferSem;
    std::function<uint64_t()> mGetMonotonicTime;
    CommitTimeChecker mCommitChecker;
    Buffer mBuffer;
    Monitor mMonitor;
    OlyServerSocket mMidgardStartupUds;
    OlyServerSocket mUtgardStartupUds;
#ifdef TCP_ANNOTATIONS
    OlyServerSocket mAnnotate;
#endif
    OlyServerSocket mAnnotateUds;
    lib::AutoClosingFd mInterruptRead {};
    lib::AutoClosingFd mInterruptWrite {};
    int mMidgardUds;
    Drivers & mDrivers;
    std::atomic_bool mSessionIsActive {true};

    void checkFlush(std::uint64_t monotonicStart, bool force)
    {
        const auto delta = mGetMonotonicTime() - monotonicStart;

        if (mCommitChecker(delta, force)) {
            mBuffer.flush();
        }
    }

    static bool isBufferOverFull(int sizeAvailable)
    {
        // if less than a quarter left
        return (sizeAvailable < (BUFFER_SIZE / 4));
    }
};

std::unique_ptr<Source> createExternalSource(sem_t & senderSem, Drivers & drivers)
{
    auto source = lib::make_unique<ExternalSource>(senderSem, drivers, &getTime);
    if (!source->prepare()) {
        return {};
    }
    return source;
}
