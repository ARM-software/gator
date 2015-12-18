/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "TtraceDriver.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

class TtraceCounter : public DriverCounter {
public:
	TtraceCounter(DriverCounter *next, char *name, int flag);
	~TtraceCounter();

	int getFlag() const { return mFlag; }

private:
	const int mFlag;

	// Intentionally unimplemented
	TtraceCounter(const TtraceCounter &);
	TtraceCounter &operator=(const TtraceCounter &);
};

TtraceCounter::TtraceCounter(DriverCounter *next, char *name, int flag) : DriverCounter(next, name), mFlag(flag) {
}

TtraceCounter::~TtraceCounter() {
}

TtraceDriver::TtraceDriver() : mSupported(false) {
}

TtraceDriver::~TtraceDriver() {
}

void TtraceDriver::readEvents(mxml_node_t *const xml) {
	if (access("/etc/tizen-release", R_OK) != 0) {
		// Reduce warning noise
		//logg.logSetup("Ttrace Disabled\n/etc/tizen-release is not found, this is not a Tizen target");
		return;
	}
	if (!gSessionData.mFtraceDriver.isSupported()) {
		logg.logSetup("Ttrace Disabled\nftrace support is required");
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

		if (strncmp(counter, "ttrace_", 7) != 0) {
			continue;
		}

		const char *flag = mxmlElementGetAttr(node, "flag");
		if (flag == NULL) {
			logg.logError("The ttrace counter %s is missing the required flag attribute", counter);
			handleException();
		}
		setCounters(new TtraceCounter(getCounters(), strdup(counter), strtol(flag, NULL, 16)));
	}
}

void TtraceDriver::setTtrace(const int flags) {
	logg.logMessage("Setting ttrace flags to %i", flags);

	const int fd = open("/tmp/ttrace_tag", O_CREAT | O_RDWR | O_CLOEXEC, 0666);
	if (fd < 0) {
		logg.logError("Unable to open /tmp/ttrace_tag");
		handleException();
	}
	if (ftruncate(fd, sizeof(uint64_t)) != 0) {
		logg.logError("ftruncate failed");
		handleException();
	}

	uint64_t *const buf = (uint64_t *)mmap(NULL, sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		logg.logError("mmap failed");
		handleException();
	}
	close(fd);

	*buf = flags;

	munmap(buf, sizeof(uint64_t));
}

void TtraceDriver::start() {
	if (!mSupported) {
		return;
	}

	int flags = 0;
	for (TtraceCounter *counter = static_cast<TtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<TtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		flags |= counter->getFlag();
	}

	setTtrace(flags);
}

void TtraceDriver::stop() {
	if (!mSupported) {
		return;
	}

	setTtrace(0);
}
