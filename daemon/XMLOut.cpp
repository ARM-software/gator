/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "XMLOut.h"

XMLOut::XMLOut() {
	indent = 0;
	incomplete = false;
	xml_string[0] = 0;
}

XMLOut::~XMLOut() {
}

void XMLOut::writeTabs() {
	for (int i = 0; i < indent; i++) {
		writeData("  ");
	}
}

void XMLOut::encodeAttributeData(const char* data) {
	if (data) {
		while (*data) {
			char ch = *data++;

			if (ch == '<') {
				writeData("&lt;");
			} else if (ch == '>') {
				writeData("&gt;");
			} else if (ch == '&') {
				writeData("&amp;");
			} else if (ch == '"') {
				writeData("&quot;");
			} else if (ch == '\'') {
				writeData("&apos;");
			} else if (ch >= ' ' && ch <= '~') {
				writeData("%c",ch);
			} else {
				writeData("&#%u;",(unsigned int)ch);
			}
		}
	}
}

void XMLOut::writeData(const char *format, ...) {
	va_list	ap;

	va_start(ap, format);
	vsnprintf(temp_buffer, sizeof(temp_buffer), format, ap);
	va_end(ap);

	strncat(xml_string, temp_buffer, sizeof(xml_string) - strlen(xml_string) - 1);
}

const XMLOut & XMLOut::xmlHeader(void) {
	writeData("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	incomplete = false;
	return *this;
}

const XMLOut & XMLOut::comment(const char* text, const bool newline) {
	if (incomplete) {
		writeData(">\n");				
	}
	writeTabs();
	writeData("<!-- %s -->", text);
	if (newline) {
		writeData("\n");
	}
	incomplete = false;
	return *this;
}

const XMLOut & XMLOut::startElement(const char* tag) {
	if (incomplete) {
		writeData(">\n");				
	}
	writeTabs();
	writeData("<%s", tag);
	incomplete = true;
	indent++;
	return *this;
}

const XMLOut & XMLOut::startElement(const char* tag, int index) {
	if (incomplete) {
		writeData(">\n");				
	}
	writeTabs();
	writeData("<!-- %d -->", index);
	writeData("<%s", tag);
	incomplete = true;
	indent++;
	return *this;
}

const XMLOut & XMLOut::endElement(const char* tag) {
	indent--;
	if (indent < 0) {
		indent = 0;	
	}
	if (incomplete) {
		writeData("/>\n");
		incomplete = false;
	} else {
		writeTabs();
		writeData("</%s>\n", tag);
	}
	return *this;
}

const XMLOut & XMLOut::attributeString(const char* name, const char* value) {
	writeData(" %s=\"", name);
	encodeAttributeData(value);
	writeData("\"");
	return *this;
}

const XMLOut & XMLOut::attributeInt(const char* name, int value) {
	writeData(" %s=\"%d\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeUInt(const char* name, unsigned int value) {
	writeData(" %s=\"%u\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeLong(const char* name, long value) {
	writeData(" %s=\"%ld\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeULong(const char* name, unsigned long value) {
	writeData(" %s=\"%lu\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeLongLong(const char* name, long long value) {
	writeData(" %s=\"%lld\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeULongLong(const char* name, unsigned long long value) {
	writeData(" %s=\"%llu\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeDouble(const char* name, double value) {
	writeData(" %s=\"%f\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeBool(const char* name, bool value) {
	writeData(" %s=\"%s\"", name, value ? "yes" : "no");
	return *this;
}

const XMLOut & XMLOut::attributeHex4(const char* name, int value) {
	writeData(" %s=\"0x%04x\"", name, value);
	return *this;
}

const XMLOut & XMLOut::attributeHex8(const char* name, int value) {
	writeData(" %s=\"0x%08x\"", name, value);
	return *this;
}
