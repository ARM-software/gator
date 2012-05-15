/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "LocalCapture.h"
#include "SessionData.h"
#include "Logging.h"
#include "OlyUtility.h"

extern void handleException();

LocalCapture::LocalCapture() {}

LocalCapture::~LocalCapture() {}

void LocalCapture::createAPCDirectory(char* target_path, char* name) {
	gSessionData->mAPCDir = createUniqueDirectory(target_path, ".apc", name);
	if ((removeDirAndAllContents(gSessionData->mAPCDir) != 0 || mkdir(gSessionData->mAPCDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)) {
		logg->logError(__FILE__, __LINE__, "Unable to create directory %s", gSessionData->mAPCDir);
		handleException();
	}
}

void LocalCapture::write(char* string) {
	char* file = (char*)malloc(PATH_MAX);

	// Set full path
	snprintf(file, PATH_MAX, "%s/session.xml", gSessionData->mAPCDir);

	// Write the file
	if (util->writeToDisk(file, string) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(file);
}

char* LocalCapture::createUniqueDirectory(const char* initialPath, const char* ending, char* title) {
	int i;
	char* output;
	char* path = (char*)malloc(PATH_MAX);

	// Ensure the path is an absolute path, i.e. starts with a slash
	if (initialPath == 0 || strlen(initialPath) == 0) {
		if (getcwd(path, PATH_MAX) == 0) {
			logg->logMessage("Unable to retrive the current working directory");
		}
		strncat(path, "/@F_@N", PATH_MAX - strlen(path) - 1);
	} else if (initialPath[0] != '/') {
		if (getcwd(path, PATH_MAX) == 0) {
			logg->logMessage("Unable to retrive the current working directory");
		}
		strncat(path, "/", PATH_MAX - strlen(path) - 1);
		strncat(path, initialPath, PATH_MAX - strlen(path) - 1);
	} else {
		strncpy(path, initialPath, PATH_MAX);
		path[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
	}

	// Convert to uppercase
	replaceAll(path, "@f", "@F", PATH_MAX);
	replaceAll(path, "@n", "@N", PATH_MAX);

	// Replace @F with the session xml title
	replaceAll(path, "@F", title, PATH_MAX);

	// Add ending if it is not already there
	if (strcmp(&path[strlen(path) - strlen(ending)], ending) != 0) {
		strncat(path, ending, PATH_MAX - strlen(path) - 1);
	}

	// Replace @N with a unique integer
	if (strstr(path, "@N")) {
		char* tempPath = (char*)malloc(PATH_MAX);
		for (i = 1; i < 1000; i++) {
			char number[4];
			snprintf(number, sizeof(number), "%03d", i);
			strcpy(tempPath, path);
			replaceAll(tempPath, "@N", number, PATH_MAX);
			struct stat mFileInfo;
			if (stat(tempPath, &mFileInfo) != 0) {
				// if the direcotry does not exist, break
				break;
			}
		}

		if (i == 1000) {
			logg->logError(__FILE__, __LINE__, "Unable to create .apc directory, please delete older directories.");
			handleException();
		}

		output = strdup(tempPath);
		free(tempPath);
	} else {
		output = strdup(path);
	}

	free(path);
	return output;
}

//Replaces all occurrences of <find> in <target> with <replace> provided enough <size> is available
void LocalCapture::replaceAll(char* target, const char* find, const char* replace, unsigned int size) {
	char* nextOccurrence;
	unsigned int count = 0;

	// Duplicate the original string
	char* original = strdup(target);
	char* ptr = original;

	// Determine number of <find>s
	ptr = strstr(ptr, find);
	while (ptr) {
		count++;
		ptr += strlen(find);
		ptr = strstr(ptr, find);
	}

	// Is there enough space available
	if (strlen(target) + (strlen(replace) - strlen(find)) * count > size - 1) {
		free(original);
		return;
	}

	// Reset
	ptr = original;

	nextOccurrence = strstr(ptr, find);
	while (nextOccurrence) {
		// Move pointers to location of replace
		int length = nextOccurrence - ptr;
		target += length;
		ptr += length;

		// Replace <find> with <replace>
		memcpy(target, replace, strlen(replace));

		// Increment over <replace>/<find>
		target += strlen(replace);
		ptr += strlen(find);

		// Copy remainder of ptr
		strcpy(target, ptr);

		// Get next occurrence
		nextOccurrence = strstr(ptr, find);
	}

	free(original);
}

int LocalCapture::removeDirAndAllContents(char* path) {
	int error = 0;
	struct stat mFileInfo;
	// Does the path exist?
	if (stat(path, &mFileInfo) == 0) {
		// Is it a directory?
		if (mFileInfo.st_mode & S_IFDIR) {
			DIR * dir = opendir(path);
			dirent* entry = readdir(dir);
			while (entry) {
				if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
					char* newpath = (char*)malloc(strlen(path) + strlen(entry->d_name) + 2);
					sprintf(newpath, "%s/%s", path, entry->d_name);
					error = removeDirAndAllContents(newpath);
					free(newpath);
					if (error) {
						break;
					}
				}
				entry = readdir(dir);
			}
			closedir(dir);
			if (error == 0) {
				error = rmdir(path);
			}
		} else {
			error = remove(path);
		}
	}
	return error;
}

void LocalCapture::copyImages(ImageLinkList* ptr) {
	char* dstfilename = (char*)malloc(PATH_MAX);

	while (ptr) {
		strncpy(dstfilename, gSessionData->mAPCDir, PATH_MAX);
		dstfilename[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
		if (gSessionData->mAPCDir[strlen(gSessionData->mAPCDir) - 1] != '/') {
			strncat(dstfilename, "/", PATH_MAX - strlen(dstfilename) - 1);
		}
		strncat(dstfilename, util->getFilePart(ptr->path), PATH_MAX - strlen(dstfilename) - 1);
		if (util->copyFile(ptr->path, dstfilename)) {
			logg->logMessage("copied file %s to %s", ptr->path, dstfilename);
		} else {
			logg->logMessage("copy of file %s to %s failed", ptr->path, dstfilename);
		}

		ptr = ptr->next;
	}
	free(dstfilename);
}
