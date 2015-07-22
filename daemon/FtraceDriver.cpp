/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceDriver.h"

#include <regex.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Config.h"
#include "DriverSource.h"
#include "Logging.h"
#include "SessionData.h"
#include "Setup.h"

class FtraceCounter : public DriverCounter {
public:
	FtraceCounter(DriverCounter *next, char *name, const char *enable);
	~FtraceCounter();

	void prepare();
	void stop();

private:
	char *const mEnable;
	int mWasEnabled;

	// Intentionally unimplemented
	FtraceCounter(const FtraceCounter &);
	FtraceCounter &operator=(const FtraceCounter &);
};

FtraceCounter::FtraceCounter(DriverCounter *next, char *name, const char *enable) : DriverCounter(next, name), mEnable(enable == NULL ? NULL : strdup(enable)) {
}

FtraceCounter::~FtraceCounter() {
	if (mEnable != NULL) {
		free(mEnable);
	}
}

void FtraceCounter::prepare() {
	if (mEnable == NULL) {
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
	if ((DriverSource::readIntDriver(buf, &mWasEnabled) != 0) ||
			(DriverSource::writeDriver(buf, 1) != 0)) {
		logg->logError("Unable to read or write to %s", buf);
		handleException();
	}
}

void FtraceCounter::stop() {
	if (mEnable == NULL) {
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
	DriverSource::writeDriver(buf, mWasEnabled);
}

FtraceDriver::FtraceDriver() : mValues(NULL), mSupported(false), mTracingOn(0) {
}

FtraceDriver::~FtraceDriver() {
	delete [] mValues;
}

void FtraceDriver::readEvents(mxml_node_t *const xml) {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg->logError("getLinuxVersion failed");
		handleException();
	}

	// The perf clock was added in 3.10
	if (KERNEL_VERSION(release[0], release[1], release[2]) < KERNEL_VERSION(3, 10, 0)) {
		mSupported = false;
		logg->logMessage("Unsupported kernel version, to use ftrace please upgrade to Linux 3.10 or later");
		return;
	}

	// Is debugfs or tracefs available?
	if (access(TRACING_PATH, R_OK) != 0) {
		mSupported = false;
		logg->logMessage("Unable to locate the tracing directory, disabling ftrace");
		return;
	}

	mSupported = true;

	mxml_node_t *node = xml;
	int count = 0;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			continue;
		}

		if (strncmp(counter, "ftrace_", 7) != 0) {
			continue;
		}

		const char *regex = mxmlElementGetAttr(node, "regex");
		if (regex == NULL) {
			logg->logError("The regex counter %s is missing the required regex attribute", counter);
			handleException();
		}

		const char *tracepoint = mxmlElementGetAttr(node, "tracepoint");
		const char *enable = mxmlElementGetAttr(node, "enable");
		if (enable == NULL) {
			enable = tracepoint;
		}
		if (gSessionData->mPerf.isSetup() && tracepoint != NULL) {
			logg->logMessage("Not using ftrace for counter %s", counter);
			continue;
		}
		if (enable != NULL) {
			char buf[1<<10];
			snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", enable);
			if (access(buf, W_OK) != 0) {
				logg->logMessage("Disabling counter %s, %s not found", counter, buf);
				continue;
			}
		}

		logg->logMessage("Using ftrace for %s", counter);
		setCounters(new FtraceCounter(getCounters(), strdup(counter), enable));
		++count;
	}

	mValues = new int64_t[2*count];
}

void FtraceDriver::prepare() {
	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->prepare();
	}

	if (DriverSource::readIntDriver(TRACING_PATH "/tracing_on", &mTracingOn)) {
		logg->logError("Unable to read if ftrace is enabled");
		handleException();
	}

	if (DriverSource::writeDriver(TRACING_PATH "/tracing_on", "0") != 0) {
		logg->logError("Unable to turn ftrace off before truncating the buffer");
		handleException();
	}

	{
		int fd;
		fd = open(TRACING_PATH "/trace", O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
		if (fd < 0) {
			logg->logError("Unable truncate ftrace buffer: %s", strerror(errno));
			handleException();
		}
		close(fd);
	}

	if (DriverSource::writeDriver(TRACING_PATH "/trace_clock", "perf") != 0) {
		logg->logError("Unable to switch ftrace to the perf clock, please ensure you are running Linux 3.10 or later");
		handleException();
	}
}

void FtraceDriver::stop() {
	DriverSource::writeDriver(TRACING_PATH "/tracing_on", mTracingOn);
	DriverSource::writeDriver(TRACING_PATH "/trace_clock", "local");

	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->stop();
	}
}
