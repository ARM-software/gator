/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef REQUEST_XML_H
#define REQUEST_XML_H

#include "XMLReader.h"

struct ConfigParameters {
	bool protocol;
	bool events;
	bool configuration;
	bool counters;
	bool session;
	bool captured;
	bool defaults;
};

class RequestXML {
public:
	RequestXML(const char * str);
	~RequestXML();
	ConfigParameters parameters;
private:
	void requestTag(XMLReader* in);
};

#endif // REQUEST_XML_H
