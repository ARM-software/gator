/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <pthread.h>

#include "DynBuf.h"

#define DRIVER_ERROR "\n Driver issue:\n  >> gator.ko must be built against the current kernel version & configuration\n  >> gator.ko should be co-located with gatord in the same directory\n  >>   OR insmod gator.ko prior to launching gatord"

class Logging
{
public:
    Logging();
    ~Logging();

    void setDebug(bool debug)
    {
        mDebug = debug;
    }

    void reset();

#define logError(...) _logError(__func__, __FILE__, __LINE__, __VA_ARGS__)
    __attribute__ ((format (printf, 5, 6)))
    void _logError(const char *function, const char *file, int line, const char *fmt, ...);
    const char *getLastError()
    {
        return mErrBuf;
    }

#define logSetup(...) _logSetup(__func__, __FILE__, __LINE__, __VA_ARGS__)
    __attribute__ ((format (printf, 5, 6)))
    void _logSetup(const char *function, const char *file, int line, const char *fmt, ...);
    const char *getSetup()
    {
        return mSetup.getBuf() == NULL ? "" : mSetup.getBuf();
    }

#define logMessage(...) _logMessage(__func__, __FILE__, __LINE__, __VA_ARGS__)
    __attribute__ ((format (printf, 5, 6)))
    void _logMessage(const char *function, const char *file, int line, const char *fmt, ...);

#define logWarning(...) _logWarning(__func__, __FILE__, __LINE__, __VA_ARGS__)
    __attribute__ ((format (printf, 5, 6)))
    void _logWarning(const char *function, const char *file, int line, const char *fmt, ...);

private:
    DynBuf mSetup;
    pthread_mutex_t mLoggingMutex;
    bool mDebug;
    char mErrBuf[8192]; // Arbitrarily large buffer to hold a string
};

extern Logging logg;

extern void handleException() __attribute__ ((noreturn));

#endif //__LOGGING_H__
