/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

typedef unsigned long long uint64_t;
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "ReadSession.h"
#include "Logging.h"

extern void handleException();

static const char*	TAG_SESSION = "session";
static const char*	TAG_IMAGE	= "image";

static const char*	ATTR_VERSION            = "version";		
static const char*	ATTR_TITLE              = "title";
static const char*	ATTR_UUID               = "uuid";
static const char*	ATTR_CALL_STACK_UNWINDING = "call_stack_unwinding";
static const char*	ATTR_BUFFER_MODE        = "buffer_mode";
static const char*	ATTR_SAMPLE_RATE        = "sample_rate";	
static const char*	ATTR_TARGET_PATH        = "target_path";
static const char*	ATTR_OUTPUT_PATH        = "output_path";
static const char*	ATTR_DURATION           = "duration";
static const char*	ATTR_PATH               = "path";

ReadSession::ReadSession(const char * str) {
	parameters.title = 0;
	parameters.uuid[0] = 0;
	parameters.target_path = 0;
	parameters.output_path = 0;
	parameters.buffer_mode[0] = 0;
	parameters.sample_rate[0] = 0;
	parameters.duration = 0;
	parameters.call_stack_unwinding = false;
	parameters.images = NULL;
	mPath = 0;
	mSessionXML = (char*)str;
	logg->logMessage(mSessionXML);
}

ReadSession::~ReadSession() {
	if (mPath != 0) {
		free(mSessionXML);
	}
}

void ReadSession::parse() {
	XMLReader reader(mSessionXML);
	char * tag = reader.nextTag();
	while(tag != 0) {
		if (strcmp(tag, TAG_SESSION) == 0) {
			sessionTag(&reader);
			return;
		}
		tag = reader.nextTag();
	}

	logg->logError(__FILE__, __LINE__, "No session tag found in the session.xml file");
	handleException();
}

void ReadSession::sessionTag(XMLReader* in) {
	char tempBuffer[PATH_MAX];
	int version = in->getAttributeAsInteger(ATTR_VERSION, 0);
	if (version != 1) {
		logg->logError(__FILE__, __LINE__, "Invalid session.xml version: %d", version);
		handleException();
	}

	in->getAttribute(ATTR_TITLE, tempBuffer, sizeof(tempBuffer), "unnamed");
	parameters.title = strdup(tempBuffer); // freed when the child process exits
	if (parameters.title == NULL) {
		logg->logError(__FILE__, __LINE__, "failed to allocate parameters.title (%d bytes)", strlen(tempBuffer));
		handleException();
	}
	in->getAttribute(ATTR_UUID, parameters.uuid, sizeof(parameters.uuid), "");
	parameters.duration = in->getAttributeAsInteger(ATTR_DURATION, 0);
	parameters.call_stack_unwinding = in->getAttributeAsBoolean(ATTR_CALL_STACK_UNWINDING, true);
	in->getAttribute(ATTR_BUFFER_MODE, parameters.buffer_mode, sizeof(parameters.buffer_mode), "normal");
	in->getAttribute(ATTR_SAMPLE_RATE, parameters.sample_rate, sizeof(parameters.sample_rate), "");
	in->getAttribute(ATTR_TARGET_PATH, tempBuffer, sizeof(tempBuffer), "");
	parameters.target_path = strdup(tempBuffer); // freed when the child process exits
	if (parameters.target_path == NULL) {
		logg->logError(__FILE__, __LINE__, "failed to allocate parameters.target_path (%d bytes)", strlen(tempBuffer));
		handleException();
	}
	in->getAttribute(ATTR_OUTPUT_PATH, tempBuffer, sizeof(tempBuffer), "");
	parameters.output_path = strdup(tempBuffer); // freed when the child process exits
	if (parameters.output_path == NULL) {
		logg->logError(__FILE__, __LINE__, "failed to allocate parameters.output_path (%d bytes)", strlen(tempBuffer));
		handleException();
	}

	char * tag = in->nextTag();
	while(tag != 0) {
		if (strcmp(tag, TAG_IMAGE) == 0) {
			sessionImage(in);
		}
		tag = in->nextTag();
	}
}

void ReadSession::sessionImage(XMLReader* in) {
	int length = in->getAttributeLength(ATTR_PATH);
	struct ImageLinkList *image;

	image = (struct ImageLinkList *)malloc(sizeof(struct ImageLinkList));
	image->path = (char *)malloc(length + 1);
	in->getAttribute(ATTR_PATH, image->path, length + 1, "");
	image->next = parameters.images;
	parameters.images = image;
}
