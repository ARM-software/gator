/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "OlyUtility.h"

#ifdef WIN32
#define MUTEX_INIT()	mLoggingMutex = CreateMutex(NULL, false, NULL);
#define MUTEX_LOCK()	WaitForSingleObject(mLoggingMutex, 0xFFFFFFFF);
#define MUTEX_UNLOCK()	ReleaseMutex(mLoggingMutex);
#define snprintf		_snprintf
#else
#include <pthread.h>
#define MUTEX_INIT()	pthread_mutex_init(&mLoggingMutex, NULL)
#define MUTEX_LOCK()	pthread_mutex_lock(&mLoggingMutex)
#define MUTEX_UNLOCK()	pthread_mutex_unlock(&mLoggingMutex)
#endif

#include "Logging.h"

// Global thread-safe logging
Logging* logg = NULL;

Logging::Logging(bool debug) {
	mFileCreated = false;
	mWarningXMLPath[0] = 0;
	mDebug = debug;
	MUTEX_INIT();

	strcpy(mErrBuf, "Unknown Error");
	strcpy(mLogBuf, "Unknown Message");
}

Logging::~Logging() {
	if (mFileCreated) {
		util->appendToDisk(mWarningXMLPath, "</warnings>");
	}
}

void Logging::logError(const char* file, int line, const char* fmt, ...) {
	va_list	args;

	MUTEX_LOCK();
	if (mDebug) {
		snprintf(mErrBuf, sizeof(mErrBuf), "ERROR[%s:%d]: ", file, line);
	} else {
		mErrBuf[0] = 0;
	}

	va_start(args, fmt);
	vsnprintf(mErrBuf + strlen(mErrBuf), sizeof(mErrBuf) - 2 - strlen(mErrBuf), fmt, args); //  subtract 2 for \n and \0
	va_end(args);

	// Add the message to the warning file if the warning file was created
	if (mWarningXMLPath[0] != 0) {
		if (!mFileCreated) {
			if (util->writeToDisk(mWarningXMLPath, "<?xml version=\"1.0\" encoding='UTF-8'?>\n<warnings version=\"1\">\n") < 0) {
				return;
			}
			mFileCreated = true;
		}
		util->appendToDisk(mWarningXMLPath, "  <warning text=\"");
		util->appendToDisk(mWarningXMLPath, mErrBuf);
		util->appendToDisk(mWarningXMLPath, "\"/>\n");
	}
	if (strlen(mErrBuf) > 0) {
		strcat(mErrBuf, "\n");
	}
	MUTEX_UNLOCK();
}

void Logging::logMessage(const char* fmt, ...) {
	if (mDebug) {
		va_list	args;

		MUTEX_LOCK();
		strcpy(mLogBuf, "INFO: ");

		va_start(args, fmt);
		vsnprintf(mLogBuf + strlen(mLogBuf), sizeof(mLogBuf) - 2 - strlen(mLogBuf), fmt, args); //  subtract 2 for \n and \0
		va_end(args);
		strcat(mLogBuf, "\n");

		fprintf(stdout, "%s", mLogBuf);
		fflush(stdout);
		MUTEX_UNLOCK();
	}
}
