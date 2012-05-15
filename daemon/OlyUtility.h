/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OLY_UTILITY_H
#define OLY_UTILITY_H

#ifdef WIN32
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

class OlyUtility {
public:
	OlyUtility() {};
	~OlyUtility() {};
	bool stringToBool(const char* string, bool defValue);
	void stringToLower(char* string);
	int getApplicationFullPath(char* path, int sizeOfPath);
	char* readFromDisk(const char* file, unsigned int *size = NULL, bool appendNull = true);
	int writeToDisk(const char* path, const char* file);
	int appendToDisk(const char* path, const char* file);
	int copyFile(const char* srcFile, const char* dstFile);
	const char* getFilePart(const char* path);
	char* getPathPart(char* path);
private:
};

#include "mxml/mxml.h"
const char * mxmlWhitespaceCB(mxml_node_t *node, int where);
extern OlyUtility* util;

#endif // OLY_UTILITY_H
