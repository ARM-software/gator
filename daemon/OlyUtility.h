/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OLY_UTILITY_H
#define OLY_UTILITY_H

#include <stddef.h>

#ifdef WIN32
#define PATH_SEPARATOR '\\'
#define CAIMAN_PATH_MAX MAX_PATH
#if !defined(_MSC_VER) || _MSC_VER < 1900
#define snprintf _snprintf
#endif
#else
#include <limits.h>
#define PATH_SEPARATOR '/'
#define CAIMAN_PATH_MAX PATH_MAX
#endif

bool stringToBool(const char* string, bool defValue);
void stringToLower(char* string);
bool stringToLongLong(long long * const value, const char *str, const int base);
bool stringToLong(long * const value, const char *str, const int base);
bool stringToInt(int * const value, const char *str, const int base);
int getApplicationFullPath(char* path, int sizeOfPath);
char* readFromDisk(const char* file, unsigned int *size = NULL, bool appendNull = true);
int writeToDisk(const char* path, const char* file);
int appendToDisk(const char* path, const char* file);
int copyFile(const char* srcFile, const char* dstFile);
const char* getFilePart(const char* path, char pathSeprator = PATH_SEPARATOR);
char* getPathPart(char* path, char pathSeprator = PATH_SEPARATOR);

#endif // OLY_UTILITY_H
