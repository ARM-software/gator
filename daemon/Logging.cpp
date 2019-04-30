/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/Time.h"

// Global thread-safe logging
Logging logg;

Logging::Logging()
        : mSetup(),
          mLoggingMutex(),
          mDebug(true),
          mErrBuf()
{
    pthread_mutex_init(&mLoggingMutex, NULL);
    reset();
}

Logging::~Logging()
{
}

void Logging::reset() {
    mSetup.reset();
    strcpy(mErrBuf, "Unknown Error");
}

static void format(char * const buf, const size_t bufSize, const bool verbose, const char * const level,
                   const char * const function, const char * const file, const int line, const char * const fmt,
                   va_list args)
{
    int len;

    if (verbose) {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        len = snprintf(buf, bufSize, "[%.7f] %s: %s(%s:%i): ", t.tv_sec + 1e-9 * t.tv_nsec, level, function, file,
                       line);
    }
    else {
        buf[0] = 0;
        len = 0;
    }

    vsnprintf(buf + len, bufSize - 1 - len, fmt, args); //  subtract 1 for \0
}

void Logging::_logError(const char *function, const char *file, int line, const char *fmt, ...)
{
    va_list args;

    pthread_mutex_lock(&mLoggingMutex);
    va_start(args, fmt);
    format(mErrBuf, sizeof(mErrBuf), mDebug, "ERROR", function, file, line, fmt, args);
    va_end(args);
    pthread_mutex_unlock(&mLoggingMutex);

    fprintf(stderr, "%s\n", mErrBuf);
}

void Logging::_logSetup(const char *function, const char *file, int line, const char *fmt, ...)
{
    char logBuf[4096]; // Arbitrarily large buffer to hold a string
    va_list args;

    va_start(args, fmt);
    format(logBuf, sizeof(logBuf), mDebug, "SETUP", function, file, line, fmt, args);
    va_end(args);

    pthread_mutex_lock(&mLoggingMutex);
    mSetup.appendStr(logBuf);
    mSetup.appendStr("|");
    pthread_mutex_unlock(&mLoggingMutex);

    if (mDebug) {
        fprintf(stderr, "%s\n", logBuf);
    }
}

void Logging::_logMessage(const char *function, const char *file, int line, const char *fmt, ...)
{
    if (mDebug) {
        char logBuf[4096]; // Arbitrarily large buffer to hold a string
        va_list args;

        pthread_mutex_lock(&mLoggingMutex);
        va_start(args, fmt);
        format(logBuf, sizeof(logBuf), mDebug, "INFO", function, file, line, fmt, args);
        va_end(args);
        pthread_mutex_unlock(&mLoggingMutex);

        fprintf(stderr, "%s\n", logBuf);
    }
}

void Logging::_logWarning(const char *function, const char *file, int line, const char *fmt, ...)
{
    char logBuf[4096]; // Arbitrarily large buffer to hold a string
    va_list args;

    pthread_mutex_lock(&mLoggingMutex);
    va_start(args, fmt);
    format(logBuf, sizeof(logBuf), mDebug, "WARNING", function, file, line, fmt, args);
    va_end(args);
    pthread_mutex_unlock(&mLoggingMutex);

    fprintf(stderr, "%s\n", logBuf);

}

