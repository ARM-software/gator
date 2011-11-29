/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "XMLReader.h"
extern void handleException();

XMLReader::XMLReader(const char* xmlstring) {
	mPtr = (char*)xmlstring;
	mNoMore = false;
	mFirstTime = true;
}

XMLReader::~XMLReader() {
}

char* XMLReader::nextTag() {
	static char tag[128]; // arbitrarily set max tag size to 127 characters + nul

	// Check if past the end of the root tag
	if (mNoMore) return NULL;

	// Find start character
	mPtr = strchr(mPtr, '<');

	if (mPtr == NULL) return mPtr;

	// Skip tag if it begins with <?
	if (mPtr[1] == '?') {
		mPtr++;
		return nextTag();
	}

	// Find end of tag name
	mPtr++;
	char* end = strchr(mPtr, ' ');
	if (end == NULL)
		end = strchr(mPtr, '>');
	if (end == NULL)
		return 0;

	// Check if tag has no attributes
	char* tagend = strchr(mPtr, '>');
	if (tagend < end) end = tagend;
	
	// Check the tag name length
	unsigned int length = (int)end - (int)mPtr;
	if (length > sizeof(tag) - 1) {
		// tag name too long, skip it
		return nextTag();
	}
	
	// Return the tag name
	strncpy(tag, mPtr, length);
	tag[length] = 0;

	// Mark the root tag
	if (mFirstTime) {
		mEndXML[0] = '/';
		strcpy(&mEndXML[1], tag);
		mFirstTime = false;
	} else if (strcmp(tag, mEndXML) == 0) {
		// End of root tag found
		mNoMore = true;
	}

	return tag;
}

void XMLReader::getAttribute(const char* name, char* value, int maxSize, const char* defValue) {
	char searchString[128];

	// Set up default
	strncpy(value, defValue, maxSize - 1);
	value[maxSize - 1] = 0;
	
	// Determine search string by ending the name with ="
	if (strlen(name) > sizeof(searchString) - 3) return;
	strcpy(searchString, name);
	strcat(searchString, "=\"");

	// Find the beginning of the attribute's search string
	char* begin = strstr(mPtr, searchString);
	if (begin == NULL) return;

	// Find the beginning of the attribute's value
	begin += strlen(searchString);

	// Find the end of the current tag to make sure the attribute exists within the tag
	char* endtag = strchr(mPtr, '>');
	if (endtag < begin) return;

	// Find the end of the attribute's value
	char* end = strchr(begin, '"');
	if (end == NULL) return;

	// Determine length
	int length = (int)end - (int)begin;
	if (length > maxSize - 1) return;

	strncpy(value, begin, length);
	value[length] = 0;
}

int XMLReader::getAttributeAsInteger(const char* name, int defValue) {
	char value[32];
	getAttribute(name, value, sizeof(value), "");
	if (value[0] == 0) return defValue;
	if (value[0] == '0' && value[1] == 'x') {
		return (int) strtoul(&value[2], (char**)NULL, 16);
	}
	return strtol(value, NULL, 10);
}

bool XMLReader::getAttributeAsBoolean(const char* name, bool defValue) {
	char value[32];
	getAttribute(name, value, sizeof(value), "");
	if (value[0] == 0) return defValue;

	// Convert to lowercase
	int i = 0;
	while (value[i]) {
		value[i] = tolower(value[i]);
		i++;
	}

	if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "1") == 0 || strcmp(value, "on") == 0) return true;
	else if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "0") == 0 || strcmp(value, "off") == 0) return false;
	else return defValue;
}

int XMLReader::getAttributeLength(const char* name) {
	char searchString[128]; // arbitrarily large amount

	// Determine search string by ending the name with ="
	if (strlen(name) > sizeof(searchString) - 3) return 0;
	strcpy(searchString, name);
	strcat(searchString, "=\"");

	// Find the beginning of the attribute's search string
	char* begin = strstr(mPtr, searchString);
	if (begin == NULL) return 0;

	// Find the beginning of the attribute's value
	begin += strlen(searchString);

	// Find the end of the current tag to make sure the attribute exists within the tag
	char* endtag = strchr(mPtr, '>');
	if (endtag < begin) return 0;

	// Find the end of the attribute's value
	char* end = strchr(begin, '"');
	if (end == NULL) return 0;

	// Determine length
	return (int)end - (int)begin;
}
