/**
 * Copyright (C) ARM Limited 2011-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "Sender.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "CapturedXML.h"
#include "StreamlineSetup.h"
#include "ConfigurationXML.h"

static const char* TAG_SESSION = "session";
static const char* TAG_REQUEST = "request";
static const char* TAG_CONFIGURATIONS = "configurations";

static const char* ATTR_TYPE           = "type";
static const char* VALUE_EVENTS        = "events";
static const char* VALUE_CONFIGURATION = "configuration";
static const char* VALUE_COUNTERS      = "counters";
static const char* VALUE_SESSION       = "session";
static const char* VALUE_CAPTURED      = "captured";
static const char* VALUE_DEFAULTS      = "defaults";

StreamlineSetup::StreamlineSetup(OlySocket* s) {
	bool ready = false;
	char* data = NULL;
	int type;

	mSocket = s;
	mSessionXML = NULL;

	// Receive commands from Streamline (master)
	while (!ready) {
		// receive command over socket
		gSessionData->mWaitingOnCommand = true;
		data = readCommand(&type);

		// parse and handle data
		switch (type) {
			case COMMAND_REQUEST_XML:
				handleRequest(data);
				break;
			case COMMAND_DELIVER_XML:
				handleDeliver(data);
				break;
			case COMMAND_APC_START:
				logg->logMessage("Received apc start request");
				ready = true;
				break;
			case COMMAND_APC_STOP:
				logg->logMessage("Received apc stop request before apc start request");
				exit(0);
				break;
			case COMMAND_DISCONNECT:
				logg->logMessage("Received disconnect command");
				exit(0);
				break;
			case COMMAND_PING:
				logg->logMessage("Received ping command");
				sendData(NULL, 0, RESPONSE_ACK);
				break;
			default:
				logg->logError(__FILE__, __LINE__, "Target error: Unknown command type, %d", type);
				handleException();
		}

		free(data);
	}

	if (gSessionData->mCounterOverflow) {
		logg->logError(__FILE__, __LINE__, "Exceeded maximum number of %d performance counters", MAX_PERFORMANCE_COUNTERS);
		handleException();
	}
}

StreamlineSetup::~StreamlineSetup() {
	if (mSessionXML) {
		free(mSessionXML);
	}
}

char* StreamlineSetup::readCommand(int* command) {
	char type;
	char* data;
	int response, length;

	// receive type
	response = mSocket->receiveNBytes(&type, sizeof(type));

	// After receiving a single byte, we are no longer waiting on a command
	gSessionData->mWaitingOnCommand = false;

	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	// receive length
	response = mSocket->receiveNBytes((char*)&length, sizeof(length));
	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	// add artificial limit
	if ((length < 0) || length > 1024 * 1024) {
		logg->logError(__FILE__, __LINE__, "Target error: Invalid length received, %d", length);
		handleException();
	}

	// allocate memory to contain the xml file, size of zero returns a zero size object
	data = (char*)calloc(length + 1, 1);
	if (data == NULL) {
		logg->logError(__FILE__, __LINE__, "Unable to allocate memory for xml");
		handleException();
	}

	// receive data
	response = mSocket->receiveNBytes(data, length);
	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	// null terminate the data for string parsing
	if (length > 0) {
		data[length] = 0;
	}

	*command = type;
	return data;
}

void StreamlineSetup::handleRequest(char* xml) {
	mxml_node_t *tree, *node;
	const char * attr = NULL;

	tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
	node = mxmlFindElement(tree, tree, TAG_REQUEST, ATTR_TYPE, NULL, MXML_DESCEND_FIRST);
	if (node) {
		attr = mxmlElementGetAttr(node, ATTR_TYPE);
	}
	if (attr && strcmp(attr, VALUE_EVENTS) == 0) {
		sendEvents();
		logg->logMessage("Sent events xml response");
	} else if (attr && strcmp(attr, VALUE_CONFIGURATION) == 0) {
		sendConfiguration();
		logg->logMessage("Sent configuration xml response");
	} else if (attr && strcmp(attr, VALUE_COUNTERS) == 0) {
		sendCounters();
		logg->logMessage("Sent counters xml response");
	} else if (attr && strcmp(attr, VALUE_SESSION) == 0) {
		sendData(mSessionXML, strlen(mSessionXML), RESPONSE_XML);
		logg->logMessage("Sent session xml response");
	} else if (attr && strcmp(attr, VALUE_CAPTURED) == 0) {
		CapturedXML capturedXML;
		char* capturedText = capturedXML.getXML(false);
		sendData(capturedText, strlen(capturedText), RESPONSE_XML);
		free(capturedText);
		logg->logMessage("Sent captured xml response");
	} else if (attr && strcmp(attr, VALUE_DEFAULTS) == 0) {
		sendDefaults();
		logg->logMessage("Sent default configuration xml response");
	} else {
		char error[] = "Unknown request";
		sendData(error, strlen(error), RESPONSE_NAK);
		logg->logMessage("Received unknown request:\n%s", xml);
	}

	mxmlDelete(tree);
}

void StreamlineSetup::handleDeliver(char* xml) {
	mxml_node_t *tree;

	// Determine xml type
	tree = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);
	if (mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND_FIRST)) {
		// Session XML
		gSessionData->parseSessionXML(xml);

		// Save xml
		mSessionXML = strdup(xml);
		if (mSessionXML == NULL) {
			logg->logError(__FILE__, __LINE__, "malloc failed for size %d", strlen(xml) + 1);
			handleException();
		}
		sendData(NULL, 0, RESPONSE_ACK);
		logg->logMessage("Received session xml");
	} else if (mxmlFindElement(tree, tree, TAG_CONFIGURATIONS, NULL, NULL, MXML_DESCEND_FIRST)) {
		// Configuration XML
		writeConfiguration(xml);
		sendData(NULL, 0, RESPONSE_ACK);
		logg->logMessage("Received configuration xml");
	} else {
		// Unknown XML
		logg->logMessage("Received unknown XML delivery type");
		sendData(NULL, 0, RESPONSE_NAK);
	}

	mxmlDelete(tree);
}

void StreamlineSetup::sendData(const char* data, int length, int type) {
	mSocket->send((char*)&type, 1);
	mSocket->send((char*)&length, sizeof(length));
	mSocket->send((char*)data, length);
}

void StreamlineSetup::sendEvents() {
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
	char* path = (char*)malloc(PATH_MAX);;
	char* buffer;
	unsigned int size = 0;

	if (gSessionData->mEventsXMLPath) {
		strncpy(path, gSessionData->mEventsXMLPath, PATH_MAX);
	} else {
		util->getApplicationFullPath(path, PATH_MAX);
		strncat(path, "events.xml", PATH_MAX - strlen(path) - 1);
	}
	buffer = util->readFromDisk(path, &size);
	if (buffer == NULL) {
		logg->logMessage("Unable to locate events.xml, using default");
		buffer = (char*)events_xml;
		size = events_xml_len;
	}

	sendData(buffer, size, RESPONSE_XML);
	if (buffer != (char*)events_xml) {
		free(buffer);
	}
	free(path);
}

void StreamlineSetup::sendConfiguration() {
	ConfigurationXML xml;

	const char* string = xml.getConfigurationXML();
	sendData(string, strlen(string), RESPONSE_XML);
}

void StreamlineSetup::sendDefaults() {
	// Send the config built into the binary
	const char* xml;
	unsigned int size;
	ConfigurationXML::getDefaultConfigurationXml(xml, size);

	// Artificial size restriction
	if (size > 1024*1024) {
		logg->logError(__FILE__, __LINE__, "Corrupt default configuration file");
		handleException();
	}

	sendData(xml, size, RESPONSE_XML);
}

#include <dirent.h>
void StreamlineSetup::sendCounters() {
	struct dirent *ent;
	mxml_node_t *xml;
	mxml_node_t *counters;
	mxml_node_t *counter;

	// counters.xml is simply a file listing of /dev/gator/events
	DIR* dir = opendir("/dev/gator/events");
	if (dir == NULL) {
		logg->logError(__FILE__, __LINE__, "Cannot create counters.xml since unable to read /dev/gator/events");
		handleException();
	}

	xml = mxmlNewXML("1.0");
	counters = mxmlNewElement(xml, "counters");
	while ((ent = readdir(dir)) != NULL) {
		// skip hidden files, current dir, and parent dir
		if (ent->d_name[0] == '.')
			continue;
		counter = mxmlNewElement(counters, "counter");
		mxmlElementSetAttr(counter, "name", ent->d_name);
	}
	closedir (dir);

	char* string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	sendString(string, RESPONSE_XML);

	free(string);
	mxmlDelete(xml);
}

void StreamlineSetup::writeConfiguration(char* xml) {
	char* path = (char*)malloc(PATH_MAX);

	if (gSessionData->mConfigurationXMLPath) {
		strncpy(path, gSessionData->mConfigurationXMLPath, PATH_MAX);
	} else {
		util->getApplicationFullPath(path, PATH_MAX);
		strncat(path, "configuration.xml", PATH_MAX - strlen(path) - 1);
	}

	if (util->writeToDisk(path, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify write permissions to this path.", path);
		handleException();
	}

	// Re-populate gSessionData with the configuration, as it has now changed
	new ConfigurationXML();
	free(path);

	if (gSessionData->mCounterOverflow) {
		logg->logError(__FILE__, __LINE__, "Exceeded maximum number of %d performance counters", MAX_PERFORMANCE_COUNTERS);
		handleException();
	}
}
