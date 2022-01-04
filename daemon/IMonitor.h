/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <memory>

#include <sys/epoll.h>

namespace gator::io {
    class IMonitor {
    public:
        virtual ~IMonitor() = default;

        virtual void close() = 0;
        virtual bool init() = 0;
        virtual bool add(int fd) = 0;
        virtual bool remove(int fd) = 0;
        virtual int wait(struct epoll_event * events, int maxevents, int timeout) const = 0;
        virtual int size() const = 0;
    };

    /**
     * This factory function should be provided by the concrete implementation
     * to supply the relevant IMonitor subclass.
     */
    extern std::unique_ptr<IMonitor> create_monitor();
}
