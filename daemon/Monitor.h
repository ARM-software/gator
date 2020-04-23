/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef MONITOR_H
#define MONITOR_H

#include <sys/epoll.h>

class Monitor {
public:
    Monitor();
    ~Monitor();

    void close();
    bool init();
    bool add(const int fd);
    int wait(struct epoll_event * const events, int maxevents, int timeout);

private:
    int mFd;

    // Intentionally unimplemented
    Monitor(const Monitor &) = delete;
    Monitor & operator=(const Monitor &) = delete;
    Monitor(Monitor &&) = delete;
    Monitor & operator=(Monitor &&) = delete;
};

#endif // MONITOR_H
