/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef MONITOR_H
#define MONITOR_H

#include <sys/epoll.h>

class Monitor {
public:
    Monitor();
    ~Monitor();

    // Intentionally unimplemented
    Monitor(const Monitor &) = delete;
    Monitor & operator=(const Monitor &) = delete;
    Monitor(Monitor &&) = delete;
    Monitor & operator=(Monitor &&) = delete;

    void close();
    bool init();
    bool add(int fd);
    bool remove(int fd);
    int wait(struct epoll_event * events, int maxevents, int timeout) const;
    int size() const { return mSize; }

private:
    int mFd;
    int mSize;
};

#endif // MONITOR_H
