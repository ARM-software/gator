/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "SessionXML.h"
#include "Logging.h"
#include "OlyUtility.h"

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

SessionXML::SessionXML(const char* str) {
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

SessionXML::~SessionXML() {
	if (mPath != 0) {
		free(mSessionXML);
	}
}

void SessionXML::parse() {
	mxml_node_t *tree;
	mxml_node_t *node;

	tree = mxmlLoadString(NULL, mSessionXML, MXML_NO_CALLBACK);
	node = mxmlFindElement(tree, tree, TAG_SESSION, NULL, NULL, MXML_DESCEND);

	if (node) {
		sessionTag(tree, node);
		mxmlDelete(tree);
		return;
	}

	logg->logError(__FILE__, __LINE__, "No session tag found in the session.xml file");
	handleException();
}

void SessionXML::sessionTag(mxml_node_t *tree, mxml_node_t *node) {
	int version = 0;
	if (mxmlElementGetAttr(node, ATTR_VERSION)) version = strtol(mxmlElementGetAttr(node, ATTR_VERSION), NULL, 10);
	if (version != 1) {
		logg->logError(__FILE__, __LINE__, "Invalid session.xml version: %d", version);
		handleException();
	}

	// allocate strings
	if (mxmlElementGetAttr(node, ATTR_TITLE)) {
		parameters.title = strdup(mxmlElementGetAttr(node, ATTR_TITLE)); // freed when the child process exits
		if (parameters.title == NULL) {
			logg->logError(__FILE__, __LINE__, "failed to allocate parameters.title");
			handleException();
		}
	}
	if (mxmlElementGetAttr(node, ATTR_TARGET_PATH)) {
		parameters.target_path = strdup(mxmlElementGetAttr(node, ATTR_TARGET_PATH)); // freed when the child process exits
		if (parameters.target_path == NULL) {
			logg->logError(__FILE__, __LINE__, "failed to allocate parameters.target_path");
			handleException();
		}
	}
	if (mxmlElementGetAttr(node, ATTR_OUTPUT_PATH)) {
		parameters.output_path = strdup(mxmlElementGetAttr(node, ATTR_OUTPUT_PATH)); // freed when the child process exits
		if (parameters.output_path == NULL) {
			logg->logError(__FILE__, __LINE__, "failed to allocate parameters.output_path");
			handleException();
		}
	}

	// copy to pre-allocated strings
	if (mxmlElementGetAttr(node, ATTR_UUID)) {
		strncpy(parameters.uuid, mxmlElementGetAttr(node, ATTR_UUID), sizeof(parameters.uuid));
		parameters.uuid[sizeof(parameters.uuid) - 1] = 0; // strncpy does not guarantee a null-terminated string
	}
	if (mxmlElementGetAttr(node, ATTR_BUFFER_MODE)) {
		strncpy(parameters.buffer_mode, mxmlElementGetAttr(node, ATTR_BUFFER_MODE), sizeof(parameters.buffer_mode));
		parameters.buffer_mode[sizeof(parameters.buffer_mode) - 1] = 0; // strncpy does not guarantee a null-terminated string
	}
	if (mxmlElementGetAttr(node, ATTR_SAMPLE_RATE)) {
		strncpy(parameters.sample_rate, mxmlElementGetAttr(node, ATTR_SAMPLE_RATE), sizeof(parameters.sample_rate));
		parameters.sample_rate[sizeof(parameters.sample_rate) - 1] = 0; // strncpy does not guarantee a null-terminated string
	}

	// integers/bools
	parameters.call_stack_unwinding = util->stringToBool(mxmlElementGetAttr(node, ATTR_CALL_STACK_UNWINDING), false);
	if (mxmlElementGetAttr(node, ATTR_DURATION)) parameters.duration = strtol(mxmlElementGetAttr(node, ATTR_DURATION), NULL, 10);

	// parse subtags
	node = mxmlGetFirstChild(node);
	while (node) {
		if (mxmlGetType(node) != MXML_ELEMENT) {
			node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
			continue;
		}
		if (strcmp(TAG_IMAGE, mxmlGetElement(node)) == 0) {
			sessionImage(node);
		}
		node = mxmlWalkNext(node, tree, MXML_NO_DESCEND);
	}
}

void SessionXML::sessionImage(mxml_node_t *node) {
	int length = strlen(mxmlElementGetAttr(node, ATTR_PATH));
	struct ImageLinkList *image;

	image = (struct ImageLinkList *)malloc(sizeof(struct ImageLinkList));
	image->path = (char*)malloc(length + 1);
	image->path = strdup(mxmlElementGetAttr(node, ATTR_PATH));
	image->next = parameters.images;
	parameters.images = image;
}
