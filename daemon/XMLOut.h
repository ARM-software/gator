/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XMLOUT_H
#define __XMLOUT_H

class XMLOut {
	int indent;
	bool incomplete;
	char temp_buffer[4096];   // arbitrarilly large buffer to hold variable arguments
	char xml_string[64*1024]; // arbitrarilly large buffer to hold an xml file output by the daemon
	
	void writeTabs();
	void encodeAttributeData(const char* data);
	void writeData(const char *format, ...);

public:
	XMLOut();
	~XMLOut();
	char* getXmlString() {return xml_string;}
	void clearXmlString() {xml_string[0]=0;}
	const XMLOut & xmlHeader(void);
	const XMLOut & comment(const char* text, const bool newline);
	const XMLOut & startElement(const char* tag);
	const XMLOut & startElement(const char* tag, int index);
	const XMLOut & endElement(const char* tag);
	const XMLOut & attributeString(const char* name, const char* value);
	const XMLOut & attributeInt(const char* name, int value);
	const XMLOut & attributeUInt(const char* name, unsigned int value);
	const XMLOut & attributeLong(const char* name, long value);
	const XMLOut & attributeULong(const char* name, unsigned long value);
	const XMLOut & attributeLongLong(const char* name, long long value);
	const XMLOut & attributeULongLong(const char* name, unsigned long long value);
	const XMLOut & attributeDouble(const char* name, double value);
	const XMLOut & attributeBool(const char* name, bool value);
	const XMLOut & attributeHex4(const char* name, int value);
	const XMLOut & attributeHex8(const char* name, int value);
};

#endif // __XMLOUT_H
