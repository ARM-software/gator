/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__LOGGING_H__
#define	__LOGGING_H__

#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef WIN32
#include <windows.h>
#define GATOR_PATH_MAX MAX_PATH
#else
#include <pthread.h>
#define GATOR_PATH_MAX PATH_MAX
#endif

#define DRIVER_ERROR "\n Driver issue:\n  >> gator.ko must be built against the current kernel version & configuration\n  >> gator.ko should be co-located with gatord in the same directory\n  >>   OR insmod gator.ko prior to launching gatord"

class Logging {
public:
	Logging(bool debug);
	~Logging();
	void logError(const char* file, int line, const char* fmt, ...);
	void logMessage(const char* fmt, ...);
	void SetWarningFile(char* path) {strncpy(mWarningXMLPath, path, GATOR_PATH_MAX); mWarningXMLPath[GATOR_PATH_MAX - 1] = 0;}
	char* getLastError() {return mErrBuf;}
	char* getLastMessage() {return mLogBuf;}

private:
	char	mWarningXMLPath[GATOR_PATH_MAX];
	char	mErrBuf[4096]; // Arbitrarily large buffer to hold a string
	char	mLogBuf[4096]; // Arbitrarily large buffer to hold a string
	bool	mDebug;
	bool	mFileCreated;
#ifdef WIN32
	HANDLE	mLoggingMutex;
#else
	pthread_mutex_t	mLoggingMutex;
#endif
};

extern Logging* logg;

#endif 	//__LOGGING_H__
