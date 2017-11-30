/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <sys/epoll.h>

#include "ClassBoilerPlate.h"

class Monitor
{
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
    CLASS_DELETE_COPY_MOVE(Monitor);
};

#endif // MONITOR_H
