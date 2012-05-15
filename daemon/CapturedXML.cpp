/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

mxml_node_t* CapturedXML::getTree() {
	bool perfCounters = false;
	mxml_node_t *xml;
    mxml_node_t *captured;
    mxml_node_t *target;
    mxml_node_t *counters;
	mxml_node_t *counter;
	int x;

	for (x=0; x<MAX_PERFORMANCE_COUNTERS; x++) {
		if (gSessionData->mPerfCounterEnabled[x]) {
			perfCounters = true;
			break;
		}
	}

	xml = mxmlNewXML("1.0");

	captured = mxmlNewElement(xml, "captured");
	mxmlElementSetAttr(captured, "version", "1");
	mxmlElementSetAttrf(captured, "protocol", "%d", PROTOCOL_VERSION);
	if (gSessionData->mBytes > 0) { // Send the following only after the capture is complete
		if (time(NULL) > 1267000000) { // If the time is reasonable (after Feb 23, 2010)
			mxmlElementSetAttrf(captured, "created", "%lu", time(NULL)); // Valid until the year 2038
		}
		mxmlElementSetAttrf(captured, "bytes", "%d", gSessionData->mBytes);
	}

	target = mxmlNewElement(captured, "target");
	mxmlElementSetAttr(target, "name", gSessionData->mCoreName);
	mxmlElementSetAttrf(target, "sample_rate", "%d", gSessionData->mSampleRate);
	mxmlElementSetAttrf(target, "cores", "%d", gSessionData->mCores);

	if (perfCounters) {
		counters = mxmlNewElement(captured, "counters");
		for (x = 0; x < MAX_PERFORMANCE_COUNTERS; x++) {
			if (gSessionData->mPerfCounterEnabled[x]) {
				counter = mxmlNewElement(counters, "counter");
				mxmlElementSetAttr(counter, "title", gSessionData->mPerfCounterTitle[x]);
				mxmlElementSetAttr(counter, "name", gSessionData->mPerfCounterName[x]);
				mxmlElementSetAttrf(counter, "color", "0x%08x", gSessionData->mPerfCounterColor[x]);
				mxmlElementSetAttrf(counter, "key", "0x%08x", gSessionData->mPerfCounterKey[x]);
				mxmlElementSetAttr(counter, "type", gSessionData->mPerfCounterType[x]);
				mxmlElementSetAttrf(counter, "event", "0x%08x", gSessionData->mPerfCounterEvent[x]);
				if (gSessionData->mPerfCounterPerCPU[x]) {
					mxmlElementSetAttr(counter, "per_cpu", "yes");
				}
				if (strlen(gSessionData->mPerfCounterOperation[x]) > 0) {
					mxmlElementSetAttr(counter, "operation", gSessionData->mPerfCounterOperation[x]);
				}
				if (gSessionData->mPerfCounterCount[x] > 0) {
					mxmlElementSetAttrf(counter, "count", "%d", gSessionData->mPerfCounterCount[x]);
				}
				if (gSessionData->mPerfCounterLevel[x]) {
					mxmlElementSetAttr(counter, "level", "yes");
				}
				if (strlen(gSessionData->mPerfCounterAlias[x]) > 0) {
					mxmlElementSetAttr(counter, "alias", gSessionData->mPerfCounterAlias[x]);
				}
				if (strlen(gSessionData->mPerfCounterDisplay[x]) > 0) {
					mxmlElementSetAttr(counter, "display", gSessionData->mPerfCounterDisplay[x]);
				}
				if (strlen(gSessionData->mPerfCounterUnits[x]) > 0) {
					mxmlElementSetAttr(counter, "units", gSessionData->mPerfCounterUnits[x]);
				}
				if (gSessionData->mPerfCounterAverageSelection[x]) {
					mxmlElementSetAttr(counter, "average_selection", "yes");
				}
				mxmlElementSetAttr(counter, "description", gSessionData->mPerfCounterDescription[x]);
			}
		}
	}

	return xml;
}

char* CapturedXML::getXML() {
	char* xml_string;
	mxml_node_t *xml = getTree();
	xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	mxmlDelete(xml);
	return xml_string;
}

void CapturedXML::write(char* path) {
	char *file = (char*)malloc(PATH_MAX);

	// Set full path
	snprintf(file, PATH_MAX, "%s/captured.xml", path);
	
	char* xml = getXML();
	if (util->writeToDisk(file, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(xml);
	free(file);
}
