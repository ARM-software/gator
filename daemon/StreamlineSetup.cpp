/**
 * Copyright (C) ARM Limited 2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

typedef long long int64_t;
typedef unsigned long long uint64_t;
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "XMLOut.h"
#include "Sender.h"
#include "Logging.h"
#include "XMLReader.h"
#include "RequestXML.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "CapturedXML.h"
#include "StreamlineSetup.h"
#include "ConfigurationXML.h"

extern void handleException();

static const char*	TAG_SESSION = "session";
static const char*	TAG_CONFIGURATIONS = "configurations";

StreamlineSetup::StreamlineSetup(OlySocket* s) {
	bool ready = false;
	char *data = NULL;
	int type;

	socket = s;
	mSessionXML = NULL;

	// Receive commands from Streamline (master)
	while (!ready) {
		// receive command over socket
		gSessionData.mWaitingOnCommand = true;
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
				// Clear error log so no text appears on console and exit
				logg->logMessage("Received apc stop request before apc start request");
				logg->logError(__FILE__, __LINE__, "");
				handleException();
				break;
			case COMMAND_DISCONNECT:
				// Clear error log so no text appears on console and exit
				logg->logMessage("Received disconnect command");
				logg->logError(__FILE__, __LINE__, "");
				handleException();
				break;
			case COMMAND_PING:
				logg->logMessage("Received ping command");
				sendData(NULL, 0, RESPONSE_ACK);
				break;
			default:
				logg->logError(__FILE__, __LINE__, "Target error: Unknown command type, %d", type);
				handleException();
		}

		delete(data);
	}
}

StreamlineSetup::~StreamlineSetup() {
	if (mSessionXML)
		free(mSessionXML);
}

char* StreamlineSetup::readCommand(int* command) {
	char type;
	char* data;
	int response, length;

	// receive type
	response = socket->receiveNBytes(&type, sizeof(type));

	// After receiving a single byte, we are no longer waiting on a command
	gSessionData.mWaitingOnCommand = false;

	if (response < 0) {
		logg->logError(__FILE__, __LINE__, "Target error: Unexpected socket disconnect");
		handleException();
	}

	// receive length
	response = socket->receiveNBytes((char*)&length, sizeof(length));
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
	response = socket->receiveNBytes(data, length);
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
	RequestXML request(xml);

	if (request.parameters.protocol) {
		sendProtocol();
		logg->logMessage("Sent protocol xml response");
	} else if (request.parameters.events) {
		sendEvents();
		logg->logMessage("Sent events xml response");
	} else if (request.parameters.configuration) {
		sendConfiguration();
		logg->logMessage("Sent configuration xml response");
	} else if (request.parameters.counters) {
		sendCounters();
		logg->logMessage("Sent counters xml response");
	} else if (request.parameters.session) {
		sendData(mSessionXML, strlen(mSessionXML), RESPONSE_XML);
		logg->logMessage("Sent session xml response");
	} else if (request.parameters.captured) {
		CapturedXML capturedXML;
		const char* capturedText = capturedXML.getXML();
		sendData(capturedText, strlen(capturedText), RESPONSE_XML);
		logg->logMessage("Sent captured xml response");
	} else if (request.parameters.defaults) {
		sendDefaults();
		logg->logMessage("Sent default configuration xml response");
	} else {
		char error[] = "Unknown request";
		sendData(error, strlen(error), RESPONSE_NAK);
		logg->logMessage("Received unknown request:\n%s", xml);
	}
}

typedef enum {UNKNOWN, SESSION_XML, CONFIGURATION_XML} delivery_type_t;
void StreamlineSetup::handleDeliver(char* xml) {
	delivery_type_t type = UNKNOWN;	

	// Determine xml type
	XMLReader reader(xml);
	char * tag = reader.nextTag();
	while(tag != 0) {
		if (strcmp(tag, TAG_SESSION) == 0) {
			type = SESSION_XML;
			break;
		} else if (strcmp(tag, TAG_CONFIGURATIONS) == 0) {
			type = CONFIGURATION_XML;
			break;
		}
		tag = reader.nextTag();
	}

	switch (type) {
		case UNKNOWN:
			logg->logMessage("Received unknown delivery type: %d", type);
			sendData(NULL, 0, RESPONSE_NAK);
			break;
		case SESSION_XML:
			// Parse the session xml
			gSessionData.parseSessionXML(xml);

			// Save xml
			mSessionXML = strdup(xml);
			if (mSessionXML == NULL) {
				logg->logError(__FILE__, __LINE__, "malloc failed for size %d", strlen(xml) + 1);
				handleException();
			}
			sendData(NULL, 0, RESPONSE_ACK);
			logg->logMessage("Received session xml");
			break;
		case CONFIGURATION_XML:
			writeConfiguration(xml);
			sendData(NULL, 0, RESPONSE_ACK);
			logg->logMessage("Received configuration xml");
			break;
	}
}

void StreamlineSetup::sendData(const char* data, int length, int type) {
	socket->send((char*)&type, 1);
	socket->send((char*)&length, sizeof(length));
	socket->send((char*)data, length);
}

void StreamlineSetup::sendProtocol() {
	XMLOut out;
	out.xmlHeader();

	out.startElement("protocol");
	out.attributeInt("version", PROTOCOL_VERSION);
	out.endElement("protocol");

	sendString(out.getXmlString(), RESPONSE_XML);
}

void StreamlineSetup::sendEvents() {
#include "events_xml.h"
	char path[PATH_MAX];
	char * buffer;
	unsigned int size = 0;

	util->getApplicationFullPath(path, sizeof(path));
	strncat(path, "events.xml", sizeof(path) - strlen(path) - 1);
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
}

void StreamlineSetup::sendConfiguration() {
	ConfigurationXML xml;

	const char* string = xml.getConfigurationXML();
	sendData(string, strlen(string), RESPONSE_XML);
}

void StreamlineSetup::sendDefaults() {
#include "configuration_xml.h"
	// Send the config built into the binary
	char* xml = (char*)configuration_xml;
	unsigned int size = configuration_xml_len;

	// Artificial size restriction
	if (size > 1024*1024) {
		logg->logError(__FILE__, __LINE__, "Corrupt default configuration file");
		handleException();
	}

	sendData(xml, size, RESPONSE_XML);
}

#include <dirent.h>
void StreamlineSetup::sendCounters() {
	XMLOut out;
	struct dirent *ent;

	// counters.xml is simply a file listing of /dev/gator/events
	DIR* dir = opendir("/dev/gator/events");
	if (dir == NULL) {
		logg->logError(__FILE__, __LINE__, "Cannot create counters.xml since unable to read /dev/gator/events");
		handleException();
	}

	out.xmlHeader();
	out.startElement("counters");
	while ((ent = readdir(dir)) != NULL) {
		// skip hidden files, current dir, and parent dir
		if (ent->d_name[0] == '.')
			continue;
		out.startElement("counter");
		out.attributeString("name", ent->d_name);
		out.endElement("counter");
	}
	out.endElement("counters");
	closedir (dir);

	sendString(out.getXmlString(), RESPONSE_XML);
}

void StreamlineSetup::writeConfiguration(char* xml) {
	char path[PATH_MAX];

	util->getApplicationFullPath(path, sizeof(path));
	strncat(path, "configuration.xml", sizeof(path) - strlen(path) - 1);
	if (util->writeToDisk(path, xml) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", path);
		handleException();
	}

	new ConfigurationXML();
}
