/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ExternalSource.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "BufferUtils.h"
#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "OlySocket.h"
#include "PrimarySourceProvider.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"
#include "Drivers.h"

static const char STREAMLINE_ANNOTATE[] = "\0streamline-annotate";
static const char MALI_VIDEO[] = "\0mali-video";
static const char MALI_VIDEO_STARTUP[] = "\0mali-video-startup";
static const char MALI_VIDEO_V1[] = "MALI_VIDEO 1\n";
static const char MALI_GRAPHICS_STARTUP[] = "\0mali_thirdparty_client";
static const char MALI_GRAPHICS_V1[] = "MALI_GRAPHICS 1\n";
static const char MALI_UTGARD_STARTUP[] = "\0mali-utgard-startup";
static const char FTRACE_V1[] = "FTRACE 1\n";
static const char FTRACE_V2[] = "FTRACE 2\n";

ExternalSource::ExternalSource(Child & child, sem_t *senderSem, Drivers & mDrivers)
        : Source(child),
          mBufferSem(),
          mBuffer(0, FrameType::EXTERNAL, 128 * 1024, senderSem),
          mMonitor(),
          mMveStartupUds(MALI_VIDEO_STARTUP, sizeof(MALI_VIDEO_STARTUP)),
          mMidgardStartupUds(MALI_GRAPHICS_STARTUP, sizeof(MALI_GRAPHICS_STARTUP)),
          mUtgardStartupUds(MALI_UTGARD_STARTUP, sizeof(MALI_UTGARD_STARTUP)),
#ifdef TCP_ANNOTATIONS
          mAnnotate(8083),
#endif
          mAnnotateUds(STREAMLINE_ANNOTATE, sizeof(STREAMLINE_ANNOTATE), true),
          mInterruptFd(-1),
          mMidgardUds(-1),
          mMveUds(-1),
          mDrivers(mDrivers)
{
    sem_init(&mBufferSem, 0, 0);
}

ExternalSource::~ExternalSource()
{
}

void ExternalSource::waitFor(const int bytes)
{
    while (mBuffer.bytesAvailable() <= bytes) {
        if (gSessionData.mOneShot && gSessionData.mSessionIsActive) {
            logg.logMessage("One shot (external)");
            mChild.endSession();
        }
        sem_wait(&mBufferSem);
    }
}

void ExternalSource::configureConnection(const int fd, const char * const handshake, size_t size)
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
    waitFor(buffer_utils::MAXSIZE_PACK32 + size - 1);
    mBuffer.packInt(fd);
    mBuffer.writeBytes(handshake, size - 1);
    mBuffer.commit(1, true);
}

bool ExternalSource::connectMidgard()
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

bool ExternalSource::connectMve()
{
    if (!mDrivers.getMaliVideo().countersEnabled()) {
        return true;
    }

    mMveUds = OlySocket::connect(MALI_VIDEO, sizeof(MALI_VIDEO));
    if (mMveUds < 0) {
        return false;
    }

    if (!mDrivers.getMaliVideo().start(mMveUds)) {
        return false;
    }

    configureConnection(mMveUds, MALI_VIDEO_V1, sizeof(MALI_VIDEO_V1));

    return true;
}

void ExternalSource::connectFtrace()
{
    if (!mDrivers.getFtraceDriver().isSupported()) {
        return;
    }

    const std::pair<std::vector<int>, bool> ftraceFds = mDrivers.getFtraceDriver().prepare();
    const char *handshake;
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

bool ExternalSource::prepare()
{
    if (!mMonitor.init() || !lib::setNonblock(mMveStartupUds.getFd()) || !mMonitor.add(mMveStartupUds.getFd())
            || !lib::setNonblock(mMidgardStartupUds.getFd()) || !mMonitor.add(mMidgardStartupUds.getFd())
            || !lib::setNonblock(mUtgardStartupUds.getFd()) || !mMonitor.add(mUtgardStartupUds.getFd())
#ifdef TCP_ANNOTATIONS
            || !lib::setNonblock(mAnnotate.getFd()) || !mMonitor.add(mAnnotate.getFd())
#endif
            || !lib::setNonblock(mAnnotateUds.getFd()) || !mMonitor.add(mAnnotateUds.getFd())) {
        return false;
    }

    connectMidgard();
    connectMve();
    connectFtrace();
    mDrivers.getExternalDriver().start();

    return true;
}

void ExternalSource::run()
{
    int pipefd[2];

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-external"), 0, 0, 0);

    // Gator runs at a high priority, reset the priority to the default
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
        logg.logError("setpriority failed");
        handleException();
    }

    if (lib::pipe_cloexec(pipefd) != 0) {
        logg.logError("pipe failed");
        handleException();
    }
    mInterruptFd = pipefd[1];

    if (!mMonitor.add(pipefd[0])) {
        logg.logError("Monitor::add failed");
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

    // Wait until monotonicStarted is set before sending data
    int64_t monotonicStarted = 0;
    while (monotonicStarted <= 0 && gSessionData.mSessionIsActive) {
        usleep(1);
        monotonicStarted = mDrivers.getPrimarySourceProvider().getMonotonicStarted();
    }

    while (gSessionData.mSessionIsActive) {
        struct epoll_event events[16];
        // Clear any pending sem posts
        while (sem_trywait(&mBufferSem) == 0)
            ;
        int ready = mMonitor.wait(events, ARRAY_LENGTH(events), -1);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }

        const uint64_t currTime = getTime() - gSessionData.mMonotonicStarted;

        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;
            if (fd == mMveStartupUds.getFd()) {
                // Mali Video Engine says it's alive
                int client = mMveStartupUds.acceptConnection();
                // Don't read from this connection, establish a new connection to Mali-V500
                close(client);
                if (!connectMve()) {
                    logg.logError("Unable to configure incoming Mali video connection");
                    handleException();
                }
            }
            else if (fd == mMidgardStartupUds.getFd()) {
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
            else if (fd == pipefd[0]) {
                // Means interrupt has been called and mSessionIsActive should be reread
            }
            else {
                /* This can result in some starvation if there are multiple
                 * threads which are annotating heavily, but it is not
                 * recommended that threads annotate that much as it can also
                 * starve out the gator data.
                 */
                while (gSessionData.mSessionIsActive) {
                    if (!transfer(currTime, fd)) {
                        break;
                    }
                }
            }
        }
    }

    if (mDrivers.getFtraceDriver().isSupported()) {
        const auto ftraceFds = mDrivers.getFtraceDriver().stop();
        // Read any slop
        const uint64_t currTime = getTime() - gSessionData.mMonotonicStarted;
        for (int fd : ftraceFds) {
            transfer(currTime, fd);
            close(fd);
        }
        mDrivers.getTtraceDriver().stop();
        mDrivers.getAtraceDriver().stop();
    }

    mBuffer.setDone();

    if (mMveUds >= 0) {
        mDrivers.getMaliVideo().stop(mMveUds);
    }

    mInterruptFd = -1;
    close(pipefd[0]);
    close(pipefd[1]);
}

bool ExternalSource::transfer(const uint64_t currTime, const int fd)
{
    // Wait until there is enough room for the fd, two headers and two ints
    waitFor(7 * buffer_utils::MAXSIZE_PACK32 + 2 * sizeof(uint32_t));
    mBuffer.packInt(fd);
    const int contiguous = mBuffer.contiguousSpaceAvailable();
    const int bytes = read(fd, mBuffer.getWritePos(), contiguous);
    if (bytes < 0) {
        if (errno == EAGAIN) {
            // Nothing left to read
            mBuffer.commit(currTime, true);
            return false;
        }
        // Something else failed, close the socket
        mBuffer.commit(currTime, true);
        mBuffer.packInt(-1);
        mBuffer.packInt(fd);
        // Here and other commits, always force-flush the buffer as this frame don't work like others
        mBuffer.commit(currTime, true);
        close(fd);
        return false;
    }
    else if (bytes == 0) {
        // The other side is closed
        mBuffer.commit(currTime, true);
        mBuffer.packInt(-1);
        mBuffer.packInt(fd);
        mBuffer.commit(currTime, true);
        close(fd);
        return false;
    }

    mBuffer.advanceWrite(bytes);
    mBuffer.commit(currTime, true);

    // Short reads also mean nothing is left to read
    return bytes >= contiguous;
}

void ExternalSource::interrupt()
{
    if (mInterruptFd >= 0) {
        int8_t c = 0;
        // Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
        if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
            logg.logError("write failed");
            handleException();
        }
    }
}

bool ExternalSource::isDone()
{
    return mBuffer.isDone();
}

void ExternalSource::write(ISender *sender)
{
    // Don't send external data until the summary packet is sent so that monotonic delta is available
    if (!gSessionData.mSentSummary) {
        return;
    }
    if (!mBuffer.isDone()) {
        mBuffer.write(sender);
        sem_post(&mBufferSem);
    }
}
