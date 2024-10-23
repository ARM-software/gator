/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef OLY_UTILITY_H
#define OLY_UTILITY_H

#include <cstddef>

#ifdef WIN32
#define PATH_SEPARATOR '\\'
#if !defined(_MSC_VER) || _MSC_VER < 1900
#define snprintf _snprintf
#endif
#else
#define PATH_SEPARATOR '/'
#endif

bool stringToBool(const char * string, bool defValue);
void stringToLower(char * string);
bool stringToLongLong(long long * value, const char * str, int base);
bool stringToLong(long * value, const char * str, int base);
bool stringToInt(int * value, const char * str, int base);
int getApplicationFullPath(char * path, int sizeOfPath);
char * readFromDisk(const char * file, unsigned int * size = nullptr, bool appendNull = true);
int writeToDisk(const char * path, const char * data);
int appendToDisk(const char * path, const char * data);
int copyFile(const char * srcFile, const char * dstFile);
const char * getFilePart(const char * path, char pathSeparator = PATH_SEPARATOR);
char * getPathPart(char * path, char pathSeparator = PATH_SEPARATOR);

#endif // OLY_UTILITY_H
