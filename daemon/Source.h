/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SOURCE_H
#define SOURCE_H

#include <pthread.h>
#include "ClassBoilerPlate.h"

class Child;
class ISender;

class Source
{
public:
    Source(Child & child);
    virtual ~Source();

    virtual bool prepare() = 0;
    void start();
    virtual void run() = 0;
    virtual void interrupt() = 0;
    void join();

    virtual bool isDone() = 0;
    virtual void write(ISender * sender) = 0;

protected:

    // active child object
    Child & mChild;

private:
    static void *runStatic(void *arg);

    pthread_t mThreadID;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(Source);
};

#endif // SOURCE_H
