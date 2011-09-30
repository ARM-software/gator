/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTERS_H
#define COUNTERS_H

#include "XMLReader.h"

class ConfigurationXML {
public:
	ConfigurationXML();
	~ConfigurationXML();
	const char* getConfigurationXML() {return mConfigurationXML;}
private:
	char* mConfigurationXML;

	int parse(const char* xmlFile);
	bool isValid(void);
	int configurationsTag(XMLReader *in);
	int configurationTag(XMLReader* in);
	int index;
};

#endif // COUNTERS_H
