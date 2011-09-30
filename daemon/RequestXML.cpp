/**
 * Copyright (C) ARM Limited 2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

typedef unsigned long long uint64_t;
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "RequestXML.h"
#include "Logging.h"

extern void handleException();

static const char*	TAG_REQUEST = "request";

static const char* 	ATTR_PROTOCOL		= "protocol";		
static const char* 	ATTR_EVENTS			= "events";
static const char* 	ATTR_CONFIGURATION	= "configuration";
static const char* 	ATTR_COUNTERS		= "counters";
static const char* 	ATTR_SESSION		= "session";
static const char* 	ATTR_CAPTURED		= "captured";
static const char*	ATTR_DEFAULTS		= "defaults";

RequestXML::RequestXML(const char * str) {
	parameters.protocol = false;
	parameters.events = false;
	parameters.configuration = false;
	parameters.counters = false;
	parameters.session = false;
	parameters.captured = false;
	parameters.defaults = false;

	XMLReader reader(str);
	char * tag = reader.nextTag();
	while(tag != 0) {
		if (strcmp(tag, TAG_REQUEST) == 0) {
			requestTag(&reader);
			return;
		}
		tag = reader.nextTag();
	}

	logg->logError(__FILE__, __LINE__, "No request tag found in the request.xml file");
	handleException();
}

RequestXML::~RequestXML() {
}

void RequestXML::requestTag(XMLReader* in) {
	parameters.protocol = in->getAttributeAsBoolean(ATTR_PROTOCOL, false);
	parameters.events = in->getAttributeAsBoolean(ATTR_EVENTS, false);
	parameters.configuration = in->getAttributeAsBoolean(ATTR_CONFIGURATION, false);
	parameters.counters = in->getAttributeAsBoolean(ATTR_COUNTERS, false);
	parameters.session = in->getAttributeAsBoolean(ATTR_SESSION, false);
	parameters.captured = in->getAttributeAsBoolean(ATTR_CAPTURED, false);
	parameters.defaults = in->getAttributeAsBoolean(ATTR_DEFAULTS, false);
}
