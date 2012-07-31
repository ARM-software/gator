/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include "Collector.h"
#include "SessionData.h"
#include "Logging.h"
#include "Sender.h"

extern void handleException();

// Driver initialization independent of session settings
Collector::Collector() {
	char text[sizeof(gSessionData->mPerfCounterType[0]) + 30]; // sufficiently large to hold all /dev/gator/events/<types>/<file>

	mBufferFD = 0;

	checkVersion();

	int enable = -1;
	if (readIntDriver("/dev/gator/enable", &enable) != 0 || enable != 0) {
		logg->logError(__FILE__, __LINE__, "Driver already enabled, possibly a session is already in progress.");
		handleException();
	}

	readIntDriver("/dev/gator/cpu_cores", &gSessionData->mCores);
	if (gSessionData->mCores == 0) {
		gSessionData->mCores = 1;
	}

	mBufferSize = 0;
	if (readIntDriver("/dev/gator/buffer_size", &mBufferSize) || mBufferSize <= 0) {
		logg->logError(__FILE__, __LINE__, "Unable to read the driver buffer size");
		handleException();
	}

	getCoreName();

	enablePerfCounters();

	// Read unchanging keys from driver which are created at insmod'ing of gator.ko
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		if (gSessionData->mPerfCounterEnabled[i]) {
			snprintf(text, sizeof(text), "/dev/gator/events/%s/key", gSessionData->mPerfCounterType[i]);
			readIntDriver(text, &gSessionData->mPerfCounterKey[i]);
		}
	}
}

Collector::~Collector() {
	// Write zero for safety, as a zero should have already been written
	writeDriver("/dev/gator/enable", "0");

	// Calls event_buffer_release in the driver
	if (mBufferFD) {
		close(mBufferFD);
	}
}

#include <dirent.h>
void Collector::enablePerfCounters() {
	char text[sizeof(gSessionData->mPerfCounterType[0]) + 30]; // sufficiently large to hold all /dev/gator/events/<types>/enabled

	// Initialize all perf counters in the driver, i.e. set enabled to zero
	struct dirent *ent;
	DIR* dir = opendir("/dev/gator/events");
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			// skip hidden files, current dir, and parent dir
			if (ent->d_name[0] == '.')
				continue;
			snprintf(text, sizeof(text), "/dev/gator/events/%s/enabled", ent->d_name);
			writeDriver(text, 0);
		}
		closedir (dir);
	}

	for (int i=0; i<MAX_PERFORMANCE_COUNTERS; i++) {
		if (!gSessionData->mPerfCounterEnabled[i]) {
			continue;
		}
		snprintf(text, sizeof(text), "/dev/gator/events/%s/enabled", gSessionData->mPerfCounterType[i]);
		if (writeReadDriver(text, &gSessionData->mPerfCounterEnabled[i])) {
			// Disable those events that don't exist on this hardware platform even though they exist in configuration.xml
			gSessionData->mPerfCounterEnabled[i] = 0;
			continue;
		}
	}
}

void Collector::setupPerfCounters() {
	char base[sizeof(gSessionData->mPerfCounterType[0]) + 20]; // sufficiently large to hold all /dev/gator/events/<types>
	char text[sizeof(gSessionData->mPerfCounterType[0]) + 30]; // sufficiently large to hold all /dev/gator/events/<types>/<file>

	for (int i=0; i<MAX_PERFORMANCE_COUNTERS; i++) {
		if (!gSessionData->mPerfCounterEnabled[i]) {
			continue;
		}
		snprintf(base, sizeof(base), "/dev/gator/events/%s", gSessionData->mPerfCounterType[i]);
		snprintf(text, sizeof(text), "%s/event", base);
		writeDriver(text, gSessionData->mPerfCounterEvent[i]);
		if (gSessionData->mPerfCounterEBSCapable[i]) {
			snprintf(text, sizeof(text), "%s/count", base);
			if (access(text, F_OK) == 0) {
				if (writeReadDriver(text, &gSessionData->mPerfCounterCount[i]) && gSessionData->mPerfCounterCount[i] > 0) {
					logg->logError(__FILE__, __LINE__, "Cannot enable EBS for %s:%s with a count of %d\n", gSessionData->mPerfCounterTitle[i], gSessionData->mPerfCounterName[i], gSessionData->mPerfCounterCount[i]);
					handleException();
				}
			} else if (gSessionData->mPerfCounterCount[i] > 0) {
				logg->logError(__FILE__, __LINE__, "Event Based Sampling is only supported with kernel versions 3.0.0 and higher with CONFIG_PERF_EVENTS=y, and CONFIG_HW_PERF_EVENTS=y\n");
				handleException();
			}
		}
	}
}

void Collector::checkVersion() {
	int driver_version = 0;

	if (readIntDriver("/dev/gator/version", &driver_version) == -1) {
		logg->logError(__FILE__, __LINE__, "Error reading gator driver version");
		handleException();
	}

	// Verify the driver version matches the daemon version
	if (driver_version != PROTOCOL_VERSION) {
		if ((driver_version > PROTOCOL_DEV) || (PROTOCOL_VERSION > PROTOCOL_DEV)) {
			// One of the mismatched versions is development version
			logg->logError(__FILE__, __LINE__,
				"DEVELOPMENT BUILD MISMATCH: gator driver version \"%d\" is not in sync with gator daemon version \"%d\".\n"
				">> The following must be synchronized from engineering repository:\n"
				">> * gator driver\n"
				">> * gator daemon\n"
				">> * Streamline", driver_version, PROTOCOL_VERSION);
			handleException();
		} else {
			// Release version mismatch
			logg->logError(__FILE__, __LINE__, 
				"gator driver version \"%d\" is different than gator daemon version \"%d\".\n"
				">> Please upgrade the driver and daemon to the latest versions.", driver_version, PROTOCOL_VERSION);
			handleException();
		}
	}
}

void Collector::start() {
	// Set the maximum backtrace depth
	if (writeReadDriver("/dev/gator/backtrace_depth", &gSessionData->mBacktraceDepth)) {
		logg->logError(__FILE__, __LINE__, "Unable to set the driver backtrace depth");
		handleException();
	}

	// open the buffer which calls userspace_buffer_open() in the driver
	mBufferFD = open("/dev/gator/buffer", O_RDONLY);
	if (mBufferFD < 0) {
		logg->logError(__FILE__, __LINE__, "The gator driver did not set up properly. Please view the linux console or dmesg log for more information on the failure.");
		handleException();
	}

	// set the tick rate of the profiling timer
	if (writeReadDriver("/dev/gator/tick", &gSessionData->mSampleRate) != 0) {
		logg->logError(__FILE__, __LINE__, "Unable to set the driver tick");
		handleException();
	}

	// notify the kernel of the response type
	int response_type = gSessionData->mLocalCapture ? 0 : RESPONSE_APC_DATA;
	if (writeDriver("/dev/gator/response_type", response_type)) {
		logg->logError(__FILE__, __LINE__, "Unable to write the response type");
		handleException();
	}

	logg->logMessage("Start the driver");

	// This command makes the driver start profiling by calling gator_op_start() in the driver
	if (writeDriver("/dev/gator/enable", "1") != 0) {
		logg->logError(__FILE__, __LINE__, "The gator driver did not start properly. Please view the linux console or dmesg log for more information on the failure.");
		handleException();
	}

	lseek(mBufferFD, 0, SEEK_SET);
}

// These commands should cause the read() function in collect() to return
void Collector::stop() {
	// This will stop the driver from profiling
	if (writeDriver("/dev/gator/enable", "0") != 0) {
		logg->logMessage("Stopping kernel failed");
	}
}

int Collector::collect(char* buffer) {
	// Calls event_buffer_read in the driver
	int bytesRead = read(mBufferFD, buffer, mBufferSize);

	// If read() returned due to an interrupt signal, re-read to obtain the last bit of collected data
	if (bytesRead == -1 && errno == EINTR) {
		bytesRead = read(mBufferFD, buffer, mBufferSize);
	}

	// return the total bytes written
	logg->logMessage("Driver read of %d bytes", bytesRead);
	return bytesRead;
}

void Collector::getCoreName() {
	char temp[256]; // arbitrarily large amount
	strcpy(gSessionData->mCoreName, "unknown");

	FILE* f = fopen("/proc/cpuinfo", "r");	
	if (f == NULL) {
		logg->logMessage("Error opening /proc/cpuinfo\n"
			"The core name in the captured xml file will be 'unknown'.");
		return;
	}

	while (fgets(temp, sizeof(temp), f)) {
		if (strlen(temp) > 0) {
			temp[strlen(temp) - 1] = 0;	// Replace the line feed with a null
		}

		if (strstr(temp, "Hardware") != 0) {
			char* position = strchr(temp, ':');
			if (position == NULL || (unsigned int)(position - temp) + 2 >= strlen(temp)) {
				logg->logMessage("Unknown format of /proc/cpuinfo\n"
					"The core name in the captured xml file will be 'unknown'.");
				return;
			}
			strncpy(gSessionData->mCoreName, (char*)((long)position + 2), sizeof(gSessionData->mCoreName));
			gSessionData->mCoreName[sizeof(gSessionData->mCoreName) - 1] = 0; // strncpy does not guarantee a null-terminated string
			fclose(f);
			return;
		}
	}

	logg->logMessage("Could not determine core name from /proc/cpuinfo\n"
		"The core name in the captured xml file will be 'unknown'.");
	fclose(f);
}

int Collector::readIntDriver(const char* fullpath, int* value) {
	FILE* file = fopen(fullpath, "r");
	if (file == NULL) {
		return -1;
	}
	if (fscanf(file, "%u", value) != 1) {
		fclose(file);
		logg->logMessage("Invalid value in file %s", fullpath);
		return -1;
	}
	fclose(file);
	return 0;
}

int Collector::writeDriver(const char* path, int value) {
	char data[40]; // Sufficiently large to hold any integer
	snprintf(data, sizeof(data), "%d", value);
	return writeDriver(path, data);
}

int Collector::writeDriver(const char* fullpath, const char* data) {
	int fd = open(fullpath, O_WRONLY);
	if (fd < 0) {
		return -1;
	}
	if (write(fd, data, strlen(data)) < 0) {
		close(fd);
		logg->logMessage("Opened but could not write to %s", fullpath);
		return -1;
	}
	close(fd);
	return 0;
}

int Collector::writeReadDriver(const char* path, int* value) {
	if (writeDriver(path, *value) || readIntDriver(path, value)) {
		return -1;
	}
	return 0;
}
