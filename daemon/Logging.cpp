/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
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

// Global thread-safe logging
Logging logg;

Logging::Logging() : mDebug(true) {
	pthread_mutex_init(&mLoggingMutex, NULL);

	strcpy(mErrBuf, "Unknown Error");
}

Logging::~Logging() {
}

static void format(char *const buf, const size_t bufSize, const bool verbose, const char *const level, const char *const function, const char *const file, const int line, const char *const fmt, va_list args) {
	int len;

	if (verbose) {
		len = snprintf(buf, bufSize, "%s: %s(%s:%i): ", level, function, file, line);
	} else {
		buf[0] = 0;
		len = 0;
	}

	vsnprintf(buf + len, bufSize - 1 - len, fmt, args); //  subtract 1 for \0
}

void Logging::_logError(const char *function, const char *file, int line, const char *fmt, ...) {
	va_list args;

	pthread_mutex_lock(&mLoggingMutex);
	va_start(args, fmt);
	format(mErrBuf, sizeof(mErrBuf), mDebug, "ERROR", function, file, line, fmt, args);
	va_end(args);
	pthread_mutex_unlock(&mLoggingMutex);

	fprintf(stderr, "%s\n", mErrBuf);
}

void Logging::_logSetup(const char *function, const char *file, int line, const char *fmt, ...) {
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

void Logging::_logMessage(const char *function, const char *file, int line, const char *fmt, ...) {
	if (mDebug) {
		char logBuf[4096]; // Arbitrarily large buffer to hold a string
		va_list args;

		va_start(args, fmt);
		format(logBuf, sizeof(logBuf), mDebug, "INFO", function, file, line, fmt, args);
		va_end(args);

		fprintf(stderr, "%s\n", logBuf);
	}
}
