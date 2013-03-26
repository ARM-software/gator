/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "ConfigurationXML.h"
#include "Driver.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char* ATTR_COUNTER           = "counter";
static const char* ATTR_REVISION          = "revision";
static const char* ATTR_TITLE             = "title";
static const char* ATTR_NAME              = "name";
static const char* ATTR_EVENT             = "event";
static const char* ATTR_COUNT             = "count";
static const char* ATTR_PER_CPU           = "per_cpu";
static const char* ATTR_DESCRIPTION       = "description";
static const char* ATTR_EBS               = "supports_event_based_sampling";
static const char* ATTR_DISPLAY           = "display";
static const char* ATTR_UNITS             = "units";
static const char* ATTR_MODIFIER          = "modifier";
static const char* ATTR_AVERAGE_SELECTION = "average_selection";

ConfigurationXML::ConfigurationXML() {
	const char * configuration_xml;
	unsigned int configuration_xml_len;
	getDefaultConfigurationXml(configuration_xml, configuration_xml_len);
	
	char path[PATH_MAX];

	if (gSessionData->mConfigurationXMLPath) {
		strncpy(path, gSessionData->mConfigurationXMLPath, PATH_MAX);
	} else {
		if (util->getApplicationFullPath(path, PATH_MAX) != 0) {
			logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
		}
		strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
	}
	mConfigurationXML = util->readFromDisk(path);

	for (int retryCount = 0; retryCount < 2; ++retryCount) {
		if (mConfigurationXML == NULL) {
			logg->logMessage("Unable to locate configuration.xml, using default in binary");
			// null-terminate configuration_xml
			mConfigurationXML = (char*)malloc(configuration_xml_len + 1);
			memcpy(mConfigurationXML, (const void*)configuration_xml, configuration_xml_len);
			mConfigurationXML[configuration_xml_len] = 0;
		}

		int ret = parse(mConfigurationXML);
		if (ret == 1) {
			// remove configuration.xml on disk to use the default
			if (remove(path) != 0) {
				logg->logError(__FILE__, __LINE__, "Invalid configuration.xml file detected and unable to delete it. To resolve, delete configuration.xml on disk");
				handleException();
			}
			logg->logMessage("Invalid configuration.xml file detected and removed");

			// Free the current configuration and reload
			free((void*)mConfigurationXML);
			mConfigurationXML = NULL;
			continue;
		}

		break;
	}
	
	validate();
}

ConfigurationXML::~ConfigurationXML() {
	if (mConfigurationXML) {
		free((void*)mConfigurationXML);
	}
}

int ConfigurationXML::parse(const char* configurationXML) {
	mxml_node_t *tree, *node;
	int ret;

	// clear counter overflow
	gSessionData->mCounterOverflow = false;
	mIndex = 0;

	// disable all counters prior to parsing the configuration xml
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		gSessionData->mCounters[i].setEnabled(false);
	}

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
		const Counter & counter = gSessionData->mCounters[i];
		if (counter.isEnabled()) {
			if (strcmp(counter.getType(), "") == 0) {
				logg->logError(__FILE__, __LINE__, "Invalid required attribute in configuration.xml:\n  counter=\"%s\"\n  title=\"%s\"\n  name=\"%s\"\n  event=%d\n", counter.getType(), counter.getTitle(), counter.getName(), counter.getEvent());
				handleException();
			}

			// iterate through the remaining enabled performance counters
			for (int j = i + 1; j < MAX_PERFORMANCE_COUNTERS; j++) {
				const Counter & counter2 = gSessionData->mCounters[j];
				if (counter2.isEnabled()) {
					// check if the types are the same
					if (strcmp(counter.getType(), counter2.getType()) == 0) {
						logg->logError(__FILE__, __LINE__, "Duplicate performance counter type in configuration.xml: %s", counter.getType());
						handleException();
					}
				}
			}
		}
	}
}

#define CONFIGURATION_REVISION 2
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
		gSessionData->mCounterOverflow = true;
		return;
	}

	// read attributes
	Counter & counter = gSessionData->mCounters[mIndex];
	counter.clear();
	if (mxmlElementGetAttr(node, ATTR_COUNTER)) counter.setType(mxmlElementGetAttr(node, ATTR_COUNTER));
	if (mxmlElementGetAttr(node, ATTR_TITLE)) counter.setTitle(mxmlElementGetAttr(node, ATTR_TITLE));
	if (mxmlElementGetAttr(node, ATTR_NAME)) counter.setName(mxmlElementGetAttr(node, ATTR_NAME));
	if (mxmlElementGetAttr(node, ATTR_DESCRIPTION)) counter.setDescription(mxmlElementGetAttr(node, ATTR_DESCRIPTION));
	if (mxmlElementGetAttr(node, ATTR_EVENT)) counter.setEvent(strtol(mxmlElementGetAttr(node, ATTR_EVENT), NULL, 16));
	if (mxmlElementGetAttr(node, ATTR_COUNT)) counter.setCount(strtol(mxmlElementGetAttr(node, ATTR_COUNT), NULL, 10));
	if (mxmlElementGetAttr(node, ATTR_PER_CPU)) counter.setPerCPU(util->stringToBool(mxmlElementGetAttr(node, ATTR_PER_CPU), false));
	if (mxmlElementGetAttr(node, ATTR_EBS)) counter.setEBSCapable(util->stringToBool(mxmlElementGetAttr(node, ATTR_EBS), false));
	if (mxmlElementGetAttr(node, ATTR_DISPLAY)) counter.setDisplay(mxmlElementGetAttr(node, ATTR_DISPLAY));
	if (mxmlElementGetAttr(node, ATTR_UNITS)) counter.setUnits(mxmlElementGetAttr(node, ATTR_UNITS));
	if (mxmlElementGetAttr(node, ATTR_MODIFIER)) counter.setModifier(strtol(mxmlElementGetAttr(node, ATTR_MODIFIER), NULL, 10));
	if (mxmlElementGetAttr(node, ATTR_AVERAGE_SELECTION)) counter.setAverageSelection(util->stringToBool(mxmlElementGetAttr(node, ATTR_AVERAGE_SELECTION), false));
	counter.setEnabled(true);

	// Associate a driver with each counter
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		if (driver->claimCounter(counter)) {
			if (counter.getDriver() != NULL) {
				logg->logError(__FILE__, __LINE__, "More than one driver has claimed %s: %s", counter.getTitle(), counter.getName());
				handleException();
			}
			counter.setDriver(driver);
		}
	}

	// If no driver is associated with the counter, disable it
	if (counter.getDriver() == NULL) {
		logg->logMessage("No driver has claimed %s (%s: %s)", counter.getType(), counter.getTitle(), counter.getName());
		counter.setEnabled(false);
	}

	// update counter index
	mIndex++;
}

void ConfigurationXML::getDefaultConfigurationXml(const char * & xml, unsigned int & len) {
#include "configuration_xml.h" // defines and initializes char configuration_xml[] and int configuration_xml_len
	xml = (const char *)configuration_xml;
	len = configuration_xml_len;
}
