/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "AtraceDriver.h"

#include <unistd.h>

/*
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "DriverSource.h"
#include "Setup.h"
*/

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

class AtraceCounter : public DriverCounter {
public:
	AtraceCounter(DriverCounter *next, char *name, int flag);
	~AtraceCounter();

	int getFlag() const { return mFlag; }

private:
	const int mFlag;

	// Intentionally unimplemented
	AtraceCounter(const AtraceCounter &);
	AtraceCounter &operator=(const AtraceCounter &);
};

AtraceCounter::AtraceCounter(DriverCounter *next, char *name, int flag) : DriverCounter(next, name), mFlag(flag) {
}

AtraceCounter::~AtraceCounter() {
}

AtraceDriver::AtraceDriver() : mSupported(false), mNotifyPath() {
}

AtraceDriver::~AtraceDriver() {
}

void AtraceDriver::readEvents(mxml_node_t *const xml) {
	if (!gSessionData->mFtraceDriver.isSupported()) {
		logg->logMessage("Atrace support disabled, ftrace support is required");
		return;
	}
	if (access("/system/bin/setprop", X_OK) != 0) {
		logg->logMessage("Atrace support disabled, setprop is not found, this is not an Android target");
		return;
	}

	if (util->getApplicationFullPath(mNotifyPath, sizeof(mNotifyPath)) != 0) {
		logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
	}
	strncat(mNotifyPath, "notify.dex", sizeof(mNotifyPath) - strlen(mNotifyPath) - 1);
	if (access(mNotifyPath, W_OK) != 0) {
		logg->logMessage("Atrace support disabled, unable to locate notify.dex");
		return;
	}

	mSupported = true;

	mxml_node_t *node = xml;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			continue;
		}

		if (strncmp(counter, "atrace_", 7) != 0) {
			continue;
		}

		const char *flag = mxmlElementGetAttr(node, "flag");
		if (flag == NULL) {
			logg->logError("The atrace counter %s is missing the required flag attribute", counter);
			handleException();
		}
		setCounters(new AtraceCounter(getCounters(), strdup(counter), strtol(flag, NULL, 16)));
	}
}

void AtraceDriver::setAtrace(const int flags) {
	logg->logMessage("Setting atrace flags to %i\n", flags);
	pid_t pid = fork();
	if (pid < 0) {
		logg->logError("fork failed");
		handleException();
	} else if (pid == 0) {
		char buf[1<<10];
		snprintf(buf, sizeof(buf), "setprop debug.atrace.tags.enableflags %i; "
			 "dalvikvm -cp %s com.android.internal.util.WithFramework Notify", flags, mNotifyPath);
		execlp("sh", "sh", "-c", buf, NULL);
		exit(0);
	}
}

void AtraceDriver::start() {
	if (!mSupported) {
		return;
	}

	int flags = 0;
	for (AtraceCounter *counter = static_cast<AtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<AtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		flags |= counter->getFlag();
	}

	setAtrace(flags);
}

void AtraceDriver::stop() {
	if (!mSupported) {
		return;
	}

	setAtrace(0);
}
