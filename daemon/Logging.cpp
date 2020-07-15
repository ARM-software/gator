/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "Logging.h"

#include "lib/Time.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Global thread-safe logging
Logging logg;

template<std::size_t N>
constexpr std::size_t findLastSlash(const char (&str)[N],
                                    std::size_t offset = 0,
                                    std::size_t last_found = ~std::size_t(0))
{
    return ((str[offset] == '\0') ? (last_found != ~std::size_t(0) ? last_found                        //
                                                                   : offset)                           //
                                  : ((str[offset] == '/') ? findLastSlash(str, offset + 1, offset + 1) //
                                                          : findLastSlash(str, offset + 1, last_found)));
}

static constexpr std::size_t FILE_PREFIX_LEN = findLastSlash(__FILE__);
static const char FILE_PREFIX[] = __FILE__;

Logging::Logging() : mSetup(), mLoggingMutex(), mDebug(true), mErrBuf()
{
    pthread_mutex_init(&mLoggingMutex, nullptr);
    reset();
}

void Logging::reset()
{
    mSetup.reset();
    strcpy(mErrBuf, "Unknown Error");
}

static const char * stripFilePrefix(const char * file)
{
    for (std::size_t i = 0; i < FILE_PREFIX_LEN; ++i) {
        if (file[i] == '\0') {
            return file;
        }
        if (file[i] != FILE_PREFIX[i]) {
            return file;
        }
    }
    return &file[FILE_PREFIX_LEN];
}

static void format(char * const buf,
                   const size_t bufSize,
                   const bool verbose,
                   const char * const level,
                   const char * const function,
                   const char * const file,
                   const int line,
                   const char * const fmt,
                   va_list args)
{
    int len;

    if (verbose) {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        len = snprintf(buf,
                       bufSize,
                       "[%.7f] %s: %s(%s:%i): ",
                       t.tv_sec + 1e-9 * t.tv_nsec,
                       level,
                       function,
                       stripFilePrefix(file),
                       line);
    }
    else {
        buf[0] = 0;
        len = 0;
    }

    vsnprintf(buf + len, bufSize - 1 - len, fmt, args); //  subtract 1 for \0
}

void Logging::_logError(const char * function, const char * file, int line, const char * fmt, ...)
{
    va_list args;

    pthread_mutex_lock(&mLoggingMutex);
    va_start(args, fmt);
    format(mErrBuf, sizeof(mErrBuf), mDebug, "ERROR", function, file, line, fmt, args);
    va_end(args);
    pthread_mutex_unlock(&mLoggingMutex);

    fprintf(stderr, "%s\n", mErrBuf);
}

void Logging::_logSetup(const char * function, const char * file, int line, const char * fmt, ...)
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

void Logging::_logMessage(const char * function, const char * file, int line, const char * fmt, ...)
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

void Logging::_logWarning(const char * function, const char * file, int line, const char * fmt, ...)
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
