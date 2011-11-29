/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "ConfigurationXML.h"
#include "Logging.h"
#include "Collector.h"
#include "OlyUtility.h"
#include "SessionData.h"

extern void handleException();
extern Collector* collector;

static const char*	ATTR_COUNTER     = "counter";
static const char*  ATTR_VERSION     = "version";
static const char* 	ATTR_TITLE       = "title";
static const char* 	ATTR_NAME        = "name";
static const char*	ATTR_EVENT       = "event";
static const char*	ATTR_COLOR       = "color";
static const char*	ATTR_COUNT       = "count";
static const char*	ATTR_OPERATION   = "operation";
static const char*	ATTR_PER_CPU     = "per_cpu";
static const char*  ATTR_DESCRIPTION = "description";
static const char*	ATTR_EBS         = "event_based_sampling";

ConfigurationXML::ConfigurationXML() {
#include "configuration_xml.h" // defines and initializes char configuration_xml[] and int configuration_xml_len
	index = 0;
	char* path = (char *)malloc(PATH_MAX);

	if (util->getApplicationFullPath(path, PATH_MAX) != 0) {
		logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
	}
	strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
	mConfigurationXML = util->readFromDisk(path);

	if (mConfigurationXML == NULL) {
		logg->logMessage("Unable to locate configuration.xml, using default in binary");
		// null-terminate configuration_xml
		mConfigurationXML = (char*)malloc(configuration_xml_len + 1);
		memcpy(mConfigurationXML, (const void*)configuration_xml, configuration_xml_len);
		mConfigurationXML[configuration_xml_len] = 0;
	}

	gSessionData.initializeCounters();

	int ret = parse(mConfigurationXML);
	if (ret == 1) {
		// remove configuration.xml on disk to use the default
		if (remove(path) != 0) {
			logg->logError(__FILE__, __LINE__, "Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
			handleException();
		}
	} else if (ret < 0 || isValid() == false) {
		logg->logError(__FILE__, __LINE__, "Parsing of the configuration.xml file failed. Please verify configuration.xml on the target filesystem is valid or delete it to use the default.");
		handleException();
	}

	collector->enablePerfCounters();
	free(path);
}

ConfigurationXML::~ConfigurationXML() {
	if (mConfigurationXML) {
		free((void*)mConfigurationXML);
	}
}

int ConfigurationXML::parse(const char* configurationXML) {
	int ret = 0;
	XMLReader reader(configurationXML);
	char * tag = reader.nextTag();
	while(tag != 0 && ret == 0) {
		if (strcmp(tag, "configurations") == 0) {
			ret = configurationsTag(&reader);
		} else if (strcmp(tag, "configuration") == 0) {
			ret = configurationTag(&reader);
		}
		tag = reader.nextTag();
	}

	return ret;
}

bool ConfigurationXML::isValid(void) {
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		if (gSessionData.mPerfCounterEnabled[i]) {
			if (strcmp(gSessionData.mPerfCounterType[i], "") == 0 ||
					strcmp(gSessionData.mPerfCounterTitle[i], "") == 0 ||
					strcmp(gSessionData.mPerfCounterName[i], "") == 0) {
				logg->logMessage("Invalid required attribute\n  counter=\"%s\"\n  title=\"%s\"\n  name=\"%s\"\n  event=%d\n", gSessionData.mPerfCounterType[i], gSessionData.mPerfCounterTitle[i], gSessionData.mPerfCounterName[i], gSessionData.mPerfCounterEvent[i]);
				return false; // failure
			}

			// iterate through the remaining enabled performance counters
			for (int j = i + 1; j < MAX_PERFORMANCE_COUNTERS; j++) {
				if (gSessionData.mPerfCounterEnabled[j]) {
					// check if the type or device are the same
					if (strcmp(gSessionData.mPerfCounterType[i], gSessionData.mPerfCounterType[j]) == 0) {
						logg->logMessage("Duplicate performance counter type: %s", gSessionData.mPerfCounterType[i]);
						return false; // failure
					}
				}
			}
		}
	}

	return true; // success
}

#define CONFIGURATION_VERSION 1
int ConfigurationXML::configurationsTag(XMLReader *in) {
	int version = in->getAttributeAsInteger(ATTR_VERSION, 0);
	if (version != CONFIGURATION_VERSION) {
		logg->logMessage("Incompatible configuration.xml version (%d) detected. The version needs to be %d.", version, CONFIGURATION_VERSION);
		return 1; // version issue
	}
	return 0;
}

int ConfigurationXML::configurationTag(XMLReader* in) {
	// handle all other performance counters
	if (index >= MAX_PERFORMANCE_COUNTERS) {
		logg->logMessage("Invalid performance counter index: %d", index);
		return -1; // failure
	}

	// read attributes
	in->getAttribute(ATTR_COUNTER, gSessionData.mPerfCounterType[index], sizeof(gSessionData.mPerfCounterType[index]), "");
	in->getAttribute(ATTR_TITLE, gSessionData.mPerfCounterTitle[index], sizeof(gSessionData.mPerfCounterTitle[index]), "");
	in->getAttribute(ATTR_NAME, gSessionData.mPerfCounterName[index], sizeof(gSessionData.mPerfCounterName[index]), "");
	in->getAttribute(ATTR_DESCRIPTION, gSessionData.mPerfCounterDescription[index], sizeof(gSessionData.mPerfCounterDescription[index]), "");
	gSessionData.mPerfCounterEvent[index] = in->getAttributeAsInteger(ATTR_EVENT, 0);
	gSessionData.mPerfCounterCount[index] = in->getAttributeAsInteger(ATTR_COUNT, 0);
	gSessionData.mPerfCounterColor[index] = in->getAttributeAsInteger(ATTR_COLOR, 0);
	gSessionData.mPerfCounterPerCPU[index] = in->getAttributeAsBoolean(ATTR_PER_CPU, false);
	gSessionData.mPerfCounterEBSCapable[index] = in->getAttributeAsBoolean(ATTR_EBS, false);
	in->getAttribute(ATTR_OPERATION, gSessionData.mPerfCounterOperation[index], sizeof(gSessionData.mPerfCounterOperation[index]), "");
	gSessionData.mPerfCounterEnabled[index] = true;

	// update counter index
	index++;

	return 0; // success
}
