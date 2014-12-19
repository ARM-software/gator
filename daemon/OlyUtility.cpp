/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <Windows.h>
#endif

#include "OlyUtility.h"

OlyUtility* util = NULL;

void OlyUtility::stringToLower(char* string) {
	if (string == NULL)
		return;

	while (*string) {
		*string = tolower(*string);
		string++;
	}
}

// Modifies fullpath with the path part including the trailing path separator
int OlyUtility::getApplicationFullPath(char* fullpath, int sizeOfPath) {
	memset(fullpath, 0, sizeOfPath);
#ifdef WIN32
	int length = GetModuleFileName(NULL, fullpath, sizeOfPath);
#else
	int length = readlink("/proc/self/exe", fullpath, sizeOfPath);
#endif

	if (length == sizeOfPath)
		return -1;

	fullpath[length] = 0;
	fullpath = getPathPart(fullpath);

	return 0;
}

char* OlyUtility::readFromDisk(const char* file, unsigned int *size, bool appendNull) {
	// Open the file
	FILE* pFile = fopen(file, "rb");
	if (pFile==NULL) return NULL;

	// Obtain file size
	fseek(pFile , 0 , SEEK_END);
	unsigned int lSize = ftell(pFile);
	rewind(pFile);

	// Allocate memory to contain the whole file
	char* buffer = (char*)malloc(lSize + (int)appendNull);
	if (buffer == NULL) return NULL;

	// Copy the file into the buffer
	if (fread(buffer, 1, lSize, pFile) != lSize) return NULL;

	// Terminate
	fclose(pFile);

	if (appendNull)
		buffer[lSize] = 0;

	if (size)
		*size = lSize;

	return buffer;
}

int OlyUtility::writeToDisk(const char* path, const char* data) {
	// Open the file
	FILE* pFile = fopen(path, "wb");
	if (pFile == NULL) return -1;

	// Write the data to disk
	if (fwrite(data, 1, strlen(data), pFile) != strlen(data)) return -1;

	// Terminate
	fclose(pFile);
	return 0;
}

int OlyUtility::appendToDisk(const char* path, const char* data) {
	// Open the file
	FILE* pFile = fopen(path, "a");
	if (pFile == NULL) return -1;

	// Write the data to disk
	if (fwrite(data, 1, strlen(data), pFile) != strlen(data)) return -1;

	// Terminate
	fclose(pFile);
	return 0;
}

/**
 * Copies the srcFile into dstFile in 1kB chunks.
 * The dstFile will be overwritten if it exists.
 * 0 is returned on an error; otherwise 1.
 */
#define TRANSFER_SIZE 1024
int OlyUtility::copyFile(const char * srcFile, const char * dstFile) {
	char* buffer = (char*)malloc(TRANSFER_SIZE);
	FILE * f_src = fopen(srcFile,"rb");
	if (!f_src) {
		return 0;
	}
	FILE * f_dst = fopen(dstFile,"wb");
	if (!f_dst) {
		fclose(f_src);
		return 0;
	}
	while (!feof(f_src)) {
		int num_bytes_read = fread(buffer, 1, TRANSFER_SIZE, f_src);
		if (num_bytes_read < TRANSFER_SIZE && !feof(f_src)) {
			fclose(f_src);
			fclose(f_dst);
			return 0;
		}
		int num_bytes_written = fwrite(buffer, 1, num_bytes_read, f_dst);
		if (num_bytes_written != num_bytes_read) {
			fclose(f_src);
			fclose(f_dst);
			return 0;
		}
	}
	fclose(f_src);
	fclose(f_dst);
	free(buffer);
	return 1;
}

const char* OlyUtility::getFilePart(const char* path) {
	const char* last_sep = strrchr(path, PATH_SEPARATOR);

	// in case path is not a full path
	if (last_sep == NULL) {
		return path;
	}

	return (const char*)((int)last_sep + 1);
}

// getPathPart may modify the contents of path
// returns the path including the trailing path separator
char* OlyUtility::getPathPart(char* path) {
	char* last_sep = strrchr(path, PATH_SEPARATOR);

	// in case path is not a full path
	if (last_sep == NULL) {
		return 0;
	}
	*(char*)((int)last_sep + 1) = 0;

	return (path);
}
