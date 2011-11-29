/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "SessionData.h"
#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"

extern void handleException();

CapturedXML::CapturedXML() {
}

CapturedXML::~CapturedXML() {
}

const char* CapturedXML::getXML() {
	bool perfCounters = false;
	int x;

	clearXmlString();
	xmlHeader();

	for (x=0; x<MAX_PERFORMANCE_COUNTERS; x++) {
		if (gSessionData.mPerfCounterEnabled[x]) {
			perfCounters = true;
			break;
		}
	}

	startElement("captured");
	attributeInt("version", 1);
	attributeInt("protocol", PROTOCOL_VERSION);
	if (gSessionData.mBytes > 0) { // Send the following only after the capture is complete
		if (time(NULL) > 1267000000) { // If the time is reasonable (after Feb 23, 2010)
			attributeUInt("created", time(NULL)); // Valid until the year 2038
		}
		attributeUInt("bytes", gSessionData.mBytes);
	}
	startElement("target");
	attributeString("name", gSessionData.mCoreName);
	attributeInt("sample_rate", gSessionData.mSampleRate);
	attributeInt("cores", gSessionData.mCores);
	endElement("target");
	if (perfCounters) {
		startElement("counters");
		for (x = 0; x < MAX_PERFORMANCE_COUNTERS; x++) {
			if (gSessionData.mPerfCounterEnabled[x]) {
				startElement("counter");
				attributeString("title", gSessionData.mPerfCounterTitle[x]);
				attributeString("name", gSessionData.mPerfCounterName[x]);
				attributeHex8("color", gSessionData.mPerfCounterColor[x]);
				attributeHex8("key", gSessionData.mPerfCounterKey[x]);
				attributeString("type", gSessionData.mPerfCounterType[x]);
				attributeHex8("event", gSessionData.mPerfCounterEvent[x]);
				if (gSessionData.mPerfCounterPerCPU[x]) {
					attributeBool("per_cpu", true);
				}
				if (strlen(gSessionData.mPerfCounterOperation[x]) > 0) {
					attributeString("operation", gSessionData.mPerfCounterOperation[x]);
				}
				if (gSessionData.mPerfCounterCount[x] > 0) {
					attributeInt("count", gSessionData.mPerfCounterCount[x]);
				}
				attributeString("description", gSessionData.mPerfCounterDescription[x]);
				endElement("counter");
			}
		}
		endElement("counters");
	}
	endElement("captured");
	return getXmlString();
}

void CapturedXML::write(char* path) {
	char* file = (char*)malloc(PATH_MAX);

	// Set full path
	snprintf(file, PATH_MAX, "%s/captured.xml", path);
	
	// Write the file
	const char* xml = getXML();
	if (util->writeToDisk(file, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(file);
}
