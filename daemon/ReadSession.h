/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef READ_SESSION_H
#define READ_SESSION_H

#include "XMLReader.h"
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

class ReadSession {
public:
	ReadSession(const char * str);
	~ReadSession();
	void parse();
	ConfigParameters parameters;
private:
	char*  mSessionXML;
	char*  mPath;
	void sessionTag(XMLReader* in);
	void sessionImage(XMLReader* in);
};

#endif // READ_SESSION_H
