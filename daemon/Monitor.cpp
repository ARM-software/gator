/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "Monitor.h"

#include "Logging.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

Monitor::Monitor() : mFd(-1)
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
        logg.logMessage("epoll_create1 failed");
        return false;
    }

#ifndef EPOLL_CLOEXEC
    int fdf = fcntl(mFd, F_GETFD);
    if ((fdf == -1) || (fcntl(mFd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
        logg.logMessage("fcntl failed");
        ::close(mFd);
        return -1;
    }
#endif

    return true;
}

bool Monitor::add(int fd)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    if (epoll_ctl(mFd, EPOLL_CTL_ADD, fd, &event) != 0) {
        logg.logMessage("epoll_ctl failed");
        return false;
    }

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
            logg.logMessage("epoll_wait failed");
        }
    }

    return result;
}
