/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_XML_H
#define SESSION_XML_H

#include "mxml/mxml.h"
#include "SessionData.h"

struct ConfigParameters {
	char* title;		// status title
	char uuid[64];		// universal unique identifier 
	char* target_path;	// target path of where to write to disk
	char* output_path;	// host path of where to write to disk
	char buffer_mode[64];	// buffer mode, "streaming", "low", "normal", "high" defines oneshot and buffer size
	char sample_rate[64];	// capture mode, "high", "normal", or "low"
	int duration;		// length of profile in seconds
	bool call_stack_unwinding;	// whether stack unwinding is performed
	struct ImageLinkList *images;	// linked list of image strings
};

class SessionXML {
public:
	SessionXML(const char* str);
	~SessionXML();
	void parse();
	ConfigParameters parameters;
private:
	char*  mSessionXML;
	char*  mPath;
	void sessionTag(mxml_node_t *tree, mxml_node_t *node);
	void sessionImage(mxml_node_t *node);
};

#endif // SESSION_XML_H
