/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _XMLREADER_H
#define _XMLREADER_H

class XMLReader {
public:
	XMLReader(const char* xmlstring);
	~XMLReader();
	char* nextTag();
	void getAttribute(const char* name, char* value, int maxSize, const char* defValue);
	int getAttributeAsInteger(const char* name, int defValue);
	bool getAttributeAsBoolean(const char* name, bool defValue);
	int getAttributeLength(const char* name);
private:
	char* mPtr;
	bool mFirstTime, mNoMore;
	char mEndXML[128];
};

#endif // _XMLREADER_H
