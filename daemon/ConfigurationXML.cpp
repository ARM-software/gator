/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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
#include "OlyUtility.h"
#include "SessionData.h"

extern void handleException();

static const char*	ATTR_COUNTER     = "counter";
static const char*  ATTR_REVISION    = "revision";
static const char* 	ATTR_TITLE       = "title";
static const char* 	ATTR_NAME        = "name";
static const char*	ATTR_EVENT       = "event";
static const char*	ATTR_COLOR       = "color";
static const char*	ATTR_COUNT       = "count";
static const char*	ATTR_OPERATION   = "operation";
static const char*	ATTR_PER_CPU     = "per_cpu";
static const char*  ATTR_DESCRIPTION = "description";
static const char*	ATTR_EBS         = "event_based_sampling";
static const char*  ATTR_LEVEL       = "level";
static const char*  ATTR_ALIAS       = "alias";
static const char*  ATTR_DISPLAY     = "display";
static const char*  ATTR_UNITS       = "units";
static const char*  ATTR_AVERAGE_SELECTION = "average_selection";

ConfigurationXML::ConfigurationXML() {
#include "configuration_xml.h" // defines and initializes char configuration_xml[] and int configuration_xml_len
	mIndex = 0;
	char* path = (char*)malloc(PATH_MAX);

	if (gSessionData->mConfigurationXMLPath) {
		strncpy(path, gSessionData->mConfigurationXMLPath, PATH_MAX);
	} else {
		if (util->getApplicationFullPath(path, PATH_MAX) != 0) {
			logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
		}
		strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
	}
	mConfigurationXML = util->readFromDisk(path);

	if (mConfigurationXML == NULL) {
		logg->logMessage("Unable to locate configuration.xml, using default in binary");
		// null-terminate configuration_xml
		mConfigurationXML = (char*)malloc(configuration_xml_len + 1);
		memcpy(mConfigurationXML, (const void*)configuration_xml, configuration_xml_len);
		mConfigurationXML[configuration_xml_len] = 0;
	}

	// disable all counters prior to parsing the configuration xml
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		gSessionData->mPerfCounterEnabled[i] = 0;
	}

	int ret = parse(mConfigurationXML);
	if (ret == 1) {
		// remove configuration.xml on disk to use the default
		if (remove(path) != 0) {
			logg->logError(__FILE__, __LINE__, "Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
			handleException();
		}
		logg->logMessage("Invalid configuration.xml file detected and removed");
	}
	
	validate();

	free(path);
}

ConfigurationXML::~ConfigurationXML() {
	if (mConfigurationXML) {
		free((void*)mConfigurationXML);
	}
}

int ConfigurationXML::parse(const char* configurationXML) {
	mxml_node_t *tree, *node;
	int ret;

	tree = mxmlLoadString(NULL, configurationXML, MXML_NO_CALLBACK);

	node = mxmlGetFirstChild(tree);
	while (node && mxmlGetType(node) != MXML_ELEMENT)
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	
	ret = configurationsTag(node);

	node = mxmlGetFirstChild(node);
	while (node) {
		if (mxmlGetType(node) != MXML_ELEMENT) {
			node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
			continue;
		}
		configurationTag(node);
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	}

	mxmlDelete(tree);

	return ret;
}

void ConfigurationXML::validate(void) {
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		if (gSessionData->mPerfCounterEnabled[i]) {
			if (strcmp(gSessionData->mPerfCounterType[i], "") == 0) {
				logg->logError(__FILE__, __LINE__, "Invalid required attribute in configuration.xml:\n  counter=\"%s\"\n  title=\"%s\"\n  name=\"%s\"\n  event=%d\n", gSessionData->mPerfCounterType[i], gSessionData->mPerfCounterTitle[i], gSessionData->mPerfCounterName[i], gSessionData->mPerfCounterEvent[i]);
				handleException();
			}

			// iterate through the remaining enabled performance counters
			for (int j = i + 1; j < MAX_PERFORMANCE_COUNTERS; j++) {
				if (gSessionData->mPerfCounterEnabled[j]) {
					// check if the types are the same
					if (strcmp(gSessionData->mPerfCounterType[i], gSessionData->mPerfCounterType[j]) == 0) {
						logg->logError(__FILE__, __LINE__, "Duplicate performance counter type in configuration.xml: %s", gSessionData->mPerfCounterType[i]);
						handleException();
					}
				}
			}
		}
	}
}

#define CONFIGURATION_REVISION 1
int ConfigurationXML::configurationsTag(mxml_node_t *node) {
	const char* revision_string;
	
	revision_string = mxmlElementGetAttr(node, ATTR_REVISION);
	if (!revision_string) {
		return 1; //revision issue;
	}

	int revision = strtol(revision_string, NULL, 10);
	if (revision < CONFIGURATION_REVISION) {
		return 1; // revision issue
	}

	return 0;
}

void ConfigurationXML::configurationTag(mxml_node_t *node) {
	// handle all other performance counters
	if (mIndex >= MAX_PERFORMANCE_COUNTERS) {
		logg->logError(__FILE__, __LINE__, "Exceeded maximum number of %d performance counters", MAX_PERFORMANCE_COUNTERS);
		handleException();
	}

	// read attributes
	if (mxmlElementGetAttr(node, ATTR_COUNTER)) strncpy(gSessionData->mPerfCounterType[mIndex], mxmlElementGetAttr(node, ATTR_COUNTER), sizeof(gSessionData->mPerfCounterType[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_TITLE)) strncpy(gSessionData->mPerfCounterTitle[mIndex], mxmlElementGetAttr(node, ATTR_TITLE), sizeof(gSessionData->mPerfCounterTitle[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_NAME)) strncpy(gSessionData->mPerfCounterName[mIndex], mxmlElementGetAttr(node, ATTR_NAME), sizeof(gSessionData->mPerfCounterName[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_DESCRIPTION)) strncpy(gSessionData->mPerfCounterDescription[mIndex], mxmlElementGetAttr(node, ATTR_DESCRIPTION), sizeof(gSessionData->mPerfCounterDescription[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_EVENT)) gSessionData->mPerfCounterEvent[mIndex] = strtol(mxmlElementGetAttr(node, ATTR_EVENT), NULL, 16);
	if (mxmlElementGetAttr(node, ATTR_COUNT)) gSessionData->mPerfCounterCount[mIndex] = strtol(mxmlElementGetAttr(node, ATTR_COUNT), NULL, 10);
	if (mxmlElementGetAttr(node, ATTR_COLOR)) gSessionData->mPerfCounterColor[mIndex] = strtol(mxmlElementGetAttr(node, ATTR_COLOR), NULL, 16);
	if (mxmlElementGetAttr(node, ATTR_PER_CPU)) gSessionData->mPerfCounterPerCPU[mIndex] = util->stringToBool(mxmlElementGetAttr(node, ATTR_PER_CPU), false);
	if (mxmlElementGetAttr(node, ATTR_EBS)) gSessionData->mPerfCounterEBSCapable[mIndex] = util->stringToBool(mxmlElementGetAttr(node, ATTR_EBS), false);
	if (mxmlElementGetAttr(node, ATTR_OPERATION)) strncpy(gSessionData->mPerfCounterOperation[mIndex], mxmlElementGetAttr(node, ATTR_OPERATION), sizeof(gSessionData->mPerfCounterOperation[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_LEVEL)) gSessionData->mPerfCounterLevel[mIndex] = util->stringToBool(mxmlElementGetAttr(node, ATTR_LEVEL), false);
	if (mxmlElementGetAttr(node, ATTR_ALIAS)) strncpy(gSessionData->mPerfCounterAlias[mIndex], mxmlElementGetAttr(node, ATTR_ALIAS), sizeof(gSessionData->mPerfCounterAlias[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_DISPLAY)) strncpy(gSessionData->mPerfCounterDisplay[mIndex], mxmlElementGetAttr(node, ATTR_DISPLAY), sizeof(gSessionData->mPerfCounterDisplay[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_UNITS)) strncpy(gSessionData->mPerfCounterUnits[mIndex], mxmlElementGetAttr(node, ATTR_UNITS), sizeof(gSessionData->mPerfCounterUnits[mIndex]));
	if (mxmlElementGetAttr(node, ATTR_AVERAGE_SELECTION)) gSessionData->mPerfCounterAverageSelection[mIndex] = util->stringToBool(mxmlElementGetAttr(node, ATTR_AVERAGE_SELECTION), false);
	gSessionData->mPerfCounterEnabled[mIndex] = true;

	// strncpy does not guarantee a null-termianted string
	gSessionData->mPerfCounterType[mIndex][sizeof(gSessionData->mPerfCounterType[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterTitle[mIndex][sizeof(gSessionData->mPerfCounterTitle[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterName[mIndex][sizeof(gSessionData->mPerfCounterName[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterDescription[mIndex][sizeof(gSessionData->mPerfCounterDescription[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterOperation[mIndex][sizeof(gSessionData->mPerfCounterOperation[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterAlias[mIndex][sizeof(gSessionData->mPerfCounterAlias[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterDisplay[mIndex][sizeof(gSessionData->mPerfCounterDisplay[mIndex]) - 1] = 0;
	gSessionData->mPerfCounterUnits[mIndex][sizeof(gSessionData->mPerfCounterUnits[mIndex]) - 1] = 0;

	// update counter index
	mIndex++;
}
