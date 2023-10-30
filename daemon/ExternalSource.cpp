/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#define BUFFER_USE_SESSION_DATA

#include "ExternalSource.h"

#include "BlockCounterFrameBuilder.h"
#include "Buffer.h"
#include "BufferUtils.h"
#include "CommitTimeChecker.h"
#include "Drivers.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "SessionData.h"
#include "handleException.h"
#include "lib/AutoClosingFd.h"
#include "lib/FileDescriptor.h"
#include "lib/Syscall.h"

#include <atomic>
#include <mutex>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char MALI_GRAPHICS_STARTUP[] = "\0mali_thirdparty_client";
static const char MALI_GRAPHICS_V1[] = "MALI_GRAPHICS 1\n";
static const char MALI_UTGARD_STARTUP[] = "\0mali-utgard-startup";
static const char FTRACE_V1[] = "FTRACE 1\n";
static const char FTRACE_V2[] = "FTRACE 2\n";

static constexpr int MEGABYTE = 1024 * 1024;

class ExternalSourceImpl : public ExternalSource {
public:
    ExternalSourceImpl(sem_t & senderSem, Drivers & mDrivers, std::function<uint64_t()> getMonotonicTime)
        : mGetMonotonicTime(std::move(getMonotonicTime)),
          mCommitChecker(gSessionData.mLiveRate),
          mBufferSize(gSessionData.mTotalBufferSize * MEGABYTE),
          mBuffer(mBufferSize, senderSem),

          mMidgardStartupUds(MALI_GRAPHICS_STARTUP, sizeof(MALI_GRAPHICS_STARTUP)),
          mUtgardStartupUds(MALI_UTGARD_STARTUP, sizeof(MALI_UTGARD_STARTUP)),
          mMidgardUds(-1),
          mDrivers(mDrivers)
    {
        sem_init(&mBufferSem, 0, 0);
    }

    void waitFor(const int bytes, const std::function<void()> & endSession)
    {
        while (mBuffer.bytesAvailable() <= bytes) {
            if (gSessionData.mOneShot && mSessionIsActive) {
                LOG_DEBUG("One shot (external)");
                endSession();
            }
            sem_wait(&mBufferSem);
        }
    }

    void configureConnection(const int fd, const char * const handshake, size_t size)
    {
        if (!lib::setNonblock(fd)) {
            LOG_ERROR("Unable to set nonblock on fh");
            handleException();
        }

        if (!mMonitor.add(fd)) {
            LOG_ERROR("Unable to add fh to monitor");
            handleException();
        }

        // Write the handshake to the circular buffer
        waitFor(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE + buffer_utils::MAXSIZE_PACK32 + size - 1, []() {
            LOG_ERROR("Unable to configure connection, buffer too small");
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
        if (!mMonitor.init() || !lib::setNonblock(mMidgardStartupUds.getFd())
            || !mMonitor.add(mMidgardStartupUds.getFd()) || !lib::setNonblock(mUtgardStartupUds.getFd())
            || !mMonitor.add(mUtgardStartupUds.getFd())) {
            return false;
        }

        int pipefd[2];
        if (lib::pipe_cloexec(pipefd) != 0) {
            LOG_ERROR("pipe failed");
            return false;
        }
        mInterruptWrite = pipefd[1];
        mInterruptRead = pipefd[0];

        if (!mMonitor.add(pipefd[0])) {
            LOG_ERROR("Monitor::add failed");
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
        if (setpriority(PRIO_PROCESS, lib::gettid(), 0) == -1) {
            LOG_ERROR("setpriority failed");
            handleException();
        }

        // Notify annotate clients to retry connecting to gatord
        uint64_t val = 1;
        if (::write(gSessionData.mAnnotateStart, &val, sizeof(val)) != sizeof(val)) {
            LOG_DEBUG("Writing to annotate pipe failed");
        }

        struct counter_value_t {
            int core;
            int key;
            std::int64_t value;
        };

        std::vector<counter_value_t> collected_values {};

        if (mDrivers.getFtraceDriver().isSupported()) {
            mDrivers.getAtraceDriver().start();
            mDrivers.getTtraceDriver().start();
            mDrivers.getFtraceDriver().start([&collected_values](int key, int core, std::int64_t value) {
                collected_values.emplace_back(counter_value_t {core, key, value});
            });
        }

        // write the initial counter values
        {
            BlockCounterFrameBuilder counter_builder {mBuffer, {}};
            const auto timestamp = 0ULL; // delta timestamp is set to zero as it is the starting value
            bool needs_timestamp = true;
            int last_core = 0;
            for (auto const & value : collected_values) {
                bool written = false;
                while (mSessionIsActive && !written) {
                    // write the frame header
                    if (needs_timestamp) {
                        if (counter_builder.eventHeader(timestamp)) {
                            last_core = 0;
                            needs_timestamp = false;
                        }
                    }
                    // if the header is written correctly
                    if (!needs_timestamp) {
                        // try to write the core value
                        if (last_core != value.core) {
                            if (counter_builder.eventCore(value.core)) {
                                last_core = value.core;
                            }
                        }
                        // if the core was written/already correct then try to write the value
                        if (last_core == value.core) {
                            if (counter_builder.event64(value.key, value.value)) {
                                written = true;
                            }
                        }
                    }
                    // flush to make space if required
                    if (!written) {
                        if (counter_builder.flush()) {
                            needs_timestamp = true;
                        }
                    }
                }
            }
            // flush any remaining frame
            counter_builder.flush();
        }

        // start the capture
        while (mSessionIsActive) {
            struct epoll_event events[16];
            // Clear any pending sem posts
            while (sem_trywait(&mBufferSem) == 0) {
            }
            int ready = mMonitor.wait(events, ARRAY_LENGTH(events), -1);
            if (ready < 0) {
                LOG_ERROR("Monitor::wait failed");
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
                        LOG_ERROR("Unable to configure incoming Midgard graphics connection");
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
                else if (fd == *mInterruptRead) {
                    // Means interrupt has been called and mSessionIsActive should be reread
                    int8_t c = 0;
                    if (::read(*mInterruptRead, &c, sizeof(c)) != sizeof(c)) {
                        LOG_ERROR("read failed");
                        handleException();
                    }
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
            const auto ftraceFds = mDrivers.getFtraceDriver().requestStop();
            // Read any slop
            for (int fd : ftraceFds) {
                if (!lib::setBlocking(fd)) {
                    LOG_WARNING("Failed to change ftrace pipe to blocking reads. Ftrace data may be truncated");
                }

                while (transfer(monotonicStart, fd, endSession)) {
                }

                close(fd);
            }
            mDrivers.getFtraceDriver().stop();
            mDrivers.getTtraceDriver().stop();
            mDrivers.getAtraceDriver().stop();
        }

        for (auto & pair : external_agent_connections) {
            LOG_DEBUG("Closing read end %d", pair.first);
            // ask the agent to close the connection
            pair.second.first->close();
            // now close the read end of the pipe
            pair.second.second.close();
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

            // remove the closed fd from the monitor and potentially from the external_agent_connections map as well
            // [SDDAP-11662] - lock this to prevent async creating another pipe with the same fd
            std::lock_guard<std::mutex> lock {external_agent_connections_mutex};
            mMonitor.remove(fd);
            external_agent_connections.erase(fd);
            LOG_DEBUG("Closed external source pipe %d", fd);

            return false;
        }

        mBuffer.advanceWrite(bytes);
        mBuffer.endFrame();
        checkFlush(monotonicStart, isBufferOverFull(mBuffer.contiguousSpaceAvailable()));

        return true;
    }

    void interrupt() override
    {
        mSessionIsActive = false; // must set this before notifying
        int8_t c = 0;
        // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
        if (::write(*mInterruptWrite, &c, sizeof(c)) != sizeof(c)) {
            LOG_ERROR("write failed");
            handleException();
        }
    }

    bool write(ISender & sender) override
    {
        const bool isDone = mBuffer.write(sender);
        sem_post(&mBufferSem);

        return isDone;
    }

    lib::AutoClosingFd add_agent_pipe(std::unique_ptr<agents::ext_source_connection_t> connection) override
    {
        std::lock_guard<std::mutex> lock {external_agent_connections_mutex};

        std::array<int, 2> pfd {{-1, -1}};
        if (lib::pipe2(pfd, O_CLOEXEC) < 0) {
            return {};
        }

        LOG_DEBUG("Created new external source pipe (es=%d, ag=%d)", pfd[0], pfd[1]);

        lib::AutoClosingFd read {pfd[0]};
        lib::AutoClosingFd write {pfd[1]};

        if (!lib::setNonblock(*read) || !mMonitor.add(*read)) {
            return {};
        }

        external_agent_connections[pfd[0]] = {std::move(connection), std::move(read)};

        int8_t c = 0;
        // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
        if (::write(*mInterruptWrite, &c, sizeof(c)) != sizeof(c)) {
            LOG_ERROR("write failed");
            handleException();
        }

        return write;
    }

private:
    using agent_connection_t = std::pair<std::unique_ptr<agents::ext_source_connection_t>, lib::AutoClosingFd>;

    sem_t mBufferSem {};
    std::function<uint64_t()> mGetMonotonicTime;
    CommitTimeChecker mCommitChecker;
    const int mBufferSize;
    Buffer mBuffer;
    Monitor mMonitor {};
    OlyServerSocket mMidgardStartupUds;
    OlyServerSocket mUtgardStartupUds;
    std::mutex external_agent_connections_mutex {};
    std::map<int, agent_connection_t> external_agent_connections {};
    lib::AutoClosingFd mInterruptRead {};
    lib::AutoClosingFd mInterruptWrite {};
    int mMidgardUds {};
    Drivers & mDrivers;
    std::atomic_bool mSessionIsActive {true};

    void checkFlush(std::uint64_t monotonicStart, bool force)
    {
        const auto delta = mGetMonotonicTime() - monotonicStart;

        if (mCommitChecker(delta, force)) {
            mBuffer.flush();
        }
    }

    [[nodiscard]] bool isBufferOverFull(int sizeAvailable) const
    {
        // if less than a quarter left
        return (sizeAvailable < (mBufferSize / 4));
    }
};

std::shared_ptr<ExternalSource> createExternalSource(sem_t & senderSem, Drivers & drivers)
{
    auto source = std::make_shared<ExternalSourceImpl>(senderSem, drivers, &getTime);
    if (!source->prepare()) {
        return {};
    }
    return source;
}
