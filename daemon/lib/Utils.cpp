/* Copyright (c) 2018 by Arm Limited. All rights reserved. */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cinttypes>

#include "Logging.h"

#include "lib/Utils.h"
#include "lib/FsEntry.h"


namespace lib
{
    int parseLinuxVersion(struct utsname & utsname)
    {
        int version[3] = { 0, 0, 0 };

        int part = 0;
        char *ch = utsname.release;
        while (*ch >= '0' && *ch <= '9' && part < 3) {
            version[part] = 10 * version[part] + *ch - '0';

            ++ch;
            if (*ch == '.') {
                ++part;
                ++ch;
            }
        }

        return KERNEL_VERSION(version[0], version[1], version[2]);
    }

    int readIntFromFile(const char *fullpath, int &value)
    {
        const std::string string = FsEntry::create(fullpath).readFileContents();
        const char * const data = string.c_str();

        char *endptr;
        errno = 0;
        value = strtol(data, &endptr, 10);
        if (errno != 0 || *endptr != '\n') {
            logg.logMessage("Invalid value in file %s: %s", fullpath, data);
            return -1;
        }

        return 0;
    }

    int readInt64FromFile(const char *fullpath, int64_t &value)
    {
        const std::string string = FsEntry::create(fullpath).readFileContents();
        const char * const data = string.c_str();

        char *endptr;
        errno = 0;
        value = strtoll(data, &endptr, 0);
        if (errno != 0 || (data == endptr) || (*endptr != '\n' && *endptr != '\0')) {
            logg.logMessage("Invalid value in file %s: %s", fullpath, data);
            return -1;
        }

        return 0;
    }


    int writeCStringToFile(const char *fullpath, const char *data)
    {
        const lib::FsEntry fsEntry = lib::FsEntry::create(fullpath);

        if (fsEntry.canAccess(false, true, false)) {
            if (lib::writeFileContents(fsEntry, data))
                return 0;
            else {
                logg.logMessage("Opened but could not write to %s", fullpath);
                return -1;
            }
        }
        else
            return -1;
    }

    int writeIntToFile(const char *path, int value)
    {
        char data[40]; // Sufficiently large to hold any integer
        snprintf(data, sizeof(data), "%d", value);
        return writeCStringToFile(path, data);
    }

    int writeInt64ToFile(const char *path, int64_t value)
    {
        char data[40]; // Sufficiently large to hold any integer
        snprintf(data, sizeof(data), "%" PRIi64, value);
        return writeCStringToFile(path, data);
    }

    int writeReadIntInFile(const char *path, int & value)
    {
        if (writeIntToFile(path, value) || readIntFromFile(path, value)) {
            return -1;
        }
        return 0;
    }

    int writeReadInt64InFile(const char *path, int64_t & value)
    {
        if (writeInt64ToFile(path, value) || readInt64FromFile(path, value)) {
            return -1;
        }
        return 0;
    }

    std::set<int> readCpuMaskFromFile(const char * path)
    {
        std::set<int> result;

        const lib::FsEntry fsEntry = lib::FsEntry::create(path);

        if (fsEntry.canAccess(true, false, false)) {
            std::string contents = lib::readFileContents(fsEntry);

            logg.logMessage("Reading cpumask from %s", fsEntry.path().c_str());

            // split the input
            const std::size_t length = contents.length();
            std::size_t from = 0, split = 0, to = 0;

            while (to < length) {
                // move end pointer
                while (to < length) {
                    if ((contents[to] >= '0') && (contents[to] <= '9')) {
                        to += 1;
                    }
                    else if (contents[to] == '-') {
                        split = to;
                        to += 1;
                    }
                    else {
                        break;
                    }
                }

                // found a valid number (or range)
                if (from < to) {
                    if (split > from) {
                        // found range
                        contents[split] = 0;
                        contents[to] = 0;
                        int nf = (int) std::strtol(contents.c_str() + from, nullptr, 10);
                        const int nt = (int) std::strtol(contents.c_str() + split + 1, nullptr, 10);
                        while (nf <= nt) {
                            logg.logMessage("    Adding cpu %d to mask", nf);
                            result.insert(nf);
                            nf += 1;
                        }
                    }
                    else {
                        // found single item
                        contents[to] = 0;
                        const int n = (int) std::strtol(contents.c_str() + from, nullptr, 10);
                        logg.logMessage("    Adding cpu %d to mask", n);
                        result.insert(n);
                    }
                }

                // move to next item
                to += 1;
                from = to;
                split = to;
            }
        }

        return result;
    }
}

