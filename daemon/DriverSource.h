/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVERSOURCE_H
#define DRIVERSOURCE_H

#include <semaphore.h>
#include <stdint.h>

#include "Source.h"

class Buffer;
class Fifo;

class DriverSource : public Source
{
public:
    DriverSource(Child & child, sem_t & senderSem, sem_t & startProfile);
    ~DriverSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(Sender * sender) override;

    static void checkVersion();
    static int readIntDriver(const char *fullpath, int *value);
    static int readInt64Driver(const char *fullpath, int64_t *value);
    static int writeDriver(const char *fullpath, const char *data);
    static int writeDriver(const char *path, int value);
    static int writeDriver(const char *path, int64_t value);
    static int writeReadDriver(const char *path, int *value);
    static int writeReadDriver(const char *path, int64_t *value);

private:
    static void *bootstrapThreadStatic(void *arg);
    void bootstrapThread();

    Buffer *mBuffer;
    Fifo *mFifo;
    sem_t & mSenderSem;
    sem_t & mStartProfile;
    int mBufferSize;
    int mBufferFD;
    int mLength;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(DriverSource);
};

#endif // DRIVERSOURCE_H
