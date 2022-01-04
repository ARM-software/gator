/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef MONITOR_H
#define MONITOR_H

#include "IMonitor.h"

class Monitor : public gator::io::IMonitor {
public:
    Monitor() noexcept;
    virtual ~Monitor();

    // Intentionally unimplemented
    Monitor(const Monitor &) = delete;
    Monitor & operator=(const Monitor &) = delete;
    Monitor(Monitor &&) = delete;
    Monitor & operator=(Monitor &&) = delete;

    void close() override;
    bool init() override;
    bool add(int fd) override;
    bool remove(int fd) override;
    int wait(struct epoll_event * events, int maxevents, int timeout) const override;
    int size() const override { return mSize; }

private:
    int mFd;
    int mSize;
};

#endif // MONITOR_H
