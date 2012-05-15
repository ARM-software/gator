/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__LOCAL_CAPTURE_H__
#define	__LOCAL_CAPTURE_H__

#include "SessionXML.h"

class LocalCapture {
public:
	LocalCapture();
	~LocalCapture();
	void write(char* string);
	void copyImages(ImageLinkList* ptr);
	void createAPCDirectory(char* target_path, char* name);
private:
	char* createUniqueDirectory(const char* path, const char* ending, char* title);
	void replaceAll(char* target, const char* find, const char* replace, unsigned int size);
	int removeDirAndAllContents(char* path);
};

#endif 	//__LOCAL_CAPTURE_H__
