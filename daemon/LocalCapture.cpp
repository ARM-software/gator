/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#include "LocalCapture.h"

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char * createUniqueDirectory(const char * initialPath, const char * ending)
{
    char * output;
    char path[PATH_MAX];

    // Ensure the path is an absolute path, i.e. starts with a slash
    if (initialPath == nullptr || strlen(initialPath) == 0) {
        LOG_ERROR("Missing -o command line option required for a local capture.");
        handleException();
    }
    else if (initialPath[0] != '/') {
        if (getcwd(path, PATH_MAX - 1) == nullptr) {
            LOG_WARNING("Unable to retrieve the current working directory");
        }
        strncat(path, "/", PATH_MAX - strlen(path) - 1);
        strncat(path, initialPath, PATH_MAX - strlen(path) - 1);
    }
    else {
        strncpy(path, initialPath, PATH_MAX - 1);
        path[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
    }

    // Add ending if it is not already there
    if (strcmp(&path[strlen(path) - strlen(ending)], ending) != 0) {
        strncat(path, ending, PATH_MAX - strlen(path) - 1);
    }

    output = strdup(path);

    return output;
}

namespace local_capture {
    void createAPCDirectory(const char * target_path)
    {
        gSessionData.mAPCDir = createUniqueDirectory(target_path, ".apc");
        if ((removeDirAndAllContents(gSessionData.mAPCDir) != 0
             || mkdir(gSessionData.mAPCDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)) {
            LOG_ERROR("Unable to create directory %s", gSessionData.mAPCDir);
            handleException();
        }
    }

    int removeDirAndAllContents(const char * path)
    {
        int error = 0;
        struct stat mFileInfo;
        // Does the path exist?
        if (stat(path, &mFileInfo) == 0) {
            // Is it a directory?
            if ((mFileInfo.st_mode & S_IFDIR) != 0U) {
                DIR * dir = opendir(path);
                dirent * entry = readdir(dir);
                while (entry != nullptr) {
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        std::unique_ptr<char[]> newpath(new char[strlen(path) + strlen(entry->d_name) + 2]);
                        sprintf(newpath.get(), "%s/%s", path, entry->d_name);
                        error = removeDirAndAllContents(newpath.get());
                        if (error != 0) {
                            break;
                        }
                    }
                    entry = readdir(dir);
                }
                closedir(dir);
                if (error == 0) {
                    error = rmdir(path);
                }
            }
            else {
                error = remove(path);
            }
        }
        return error;
    }

    void copyImages(const std::list<std::string> & list)
    {
        char dstfilename[PATH_MAX];

        for (const auto & element : list) {
            strncpy(dstfilename, gSessionData.mAPCDir, PATH_MAX - 1);
            dstfilename[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
            if (gSessionData.mAPCDir[strlen(gSessionData.mAPCDir) - 1] != '/') {
                strncat(dstfilename, "/", PATH_MAX - strlen(dstfilename) - 1);
            }
            strncat(dstfilename, getFilePart(element.c_str()), PATH_MAX - strlen(dstfilename) - 1);
            if (copyFile(element.c_str(), dstfilename) != 0) {
                LOG_FINE("copied file %s to %s", element.c_str(), dstfilename);
            }
            else {
                LOG_WARNING("copy of file %s to %s failed", element.c_str(), dstfilename);
            }
        }
    }
}
