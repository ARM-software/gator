/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <pthread.h>

#define DRIVER_ERROR "\n Driver issue:\n  >> gator.ko must be built against the current kernel version & configuration\n  >> gator.ko should be co-located with gatord in the same directory\n  >>   OR insmod gator.ko prior to launching gatord"

class Logging {
public:
	Logging(bool debug);
	~Logging();
#define logError(...) _logError(__func__, __FILE__, __LINE__, __VA_ARGS__)
	__attribute__ ((format (printf, 5, 6)))
	void _logError(const char *function, const char *file, int line, const char *fmt, ...);
#define logMessage(...) _logMessage(__func__, __FILE__, __LINE__, __VA_ARGS__)
	__attribute__ ((format (printf, 5, 6)))
	void _logMessage(const char *function, const char *file, int line, const char *fmt, ...);
	char *getLastError() {return mErrBuf;}
	char *getLastMessage() {return mLogBuf;}

private:
	char mErrBuf[4096]; // Arbitrarily large buffer to hold a string
	char mLogBuf[4096]; // Arbitrarily large buffer to hold a string
	bool mDebug;
	pthread_mutex_t mLoggingMutex;
};

extern Logging *logg;

extern void handleException() __attribute__ ((noreturn));

#endif //__LOGGING_H__
