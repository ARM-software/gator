/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#include "Monitor.h"

#include "IMonitor.h"
#include "Logging.h"

#include <cerrno>
#include <cstring>
#include <memory>

#include <sys/epoll.h>
#include <unistd.h>

namespace gator::io {
    std::unique_ptr<IMonitor> create_monitor()
    {
        return std::make_unique<Monitor>();
    }
}

Monitor::Monitor() noexcept : mFd(-1), mSize(0)
{
}

Monitor::~Monitor()
{
    if (mFd >= 0) {
        ::close(mFd);
    }
}

void Monitor::close()
{
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
}

bool Monitor::init()
{
#ifdef EPOLL_CLOEXEC
    mFd = epoll_create1(EPOLL_CLOEXEC);
#else
    mFd = epoll_create(16);
#endif
    if (mFd < 0) {
        LOG_DEBUG("epoll_create1 failed");
        return false;
    }

#ifndef EPOLL_CLOEXEC
    int fdf = fcntl(mFd, F_GETFD);
    if ((fdf == -1) || (fcntl(mFd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
        LOG_DEBUG("fcntl failed");
        ::close(mFd);
        return false;
    }
#endif

    mSize = 0;

    return true;
}

static bool addOrRemove(int mFd, int fd, bool add)
{
    const int op = (add ? EPOLL_CTL_ADD : EPOLL_CTL_DEL);

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    if (epoll_ctl(mFd, op, fd, &event) != 0) {
        LOG_DEBUG("epoll_ctl failed");
        return false;
    }
    return true;
}

bool Monitor::add(int fd)
{
    if (!addOrRemove(mFd, fd, true)) {
        return false;
    }
    mSize += 1;
    return true;
}

bool Monitor::remove(int fd)
{
    if (!addOrRemove(mFd, fd, false)) {
        return false;
    }
    mSize -= 1;
    return true;
}

int Monitor::wait(struct epoll_event * const events, int maxevents, int timeout) const
{
    int result = epoll_wait(mFd, events, maxevents, timeout);
    if (result < 0) {
        // Ignore if the call was interrupted as this will happen when SIGINT is received
        if (errno == EINTR) {
            result = 0;
        }
        else {
            LOG_DEBUG("epoll_wait failed");
        }
    }

    return result;
}
