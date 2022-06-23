/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_UTILS_H
#define INCLUDE_LIB_UTILS_H

#include <cstdint>
#include <set>

#include <linux/version.h>
#include <sys/utsname.h>

namespace lib {

    int parseLinuxVersion(struct utsname & utsname);

    int readIntFromFile(const char * fullpath, int & value);
    int readInt64FromFile(const char * fullpath, int64_t & value);

    int writeCStringToFile(const char * fullpath, const char * data);
    int writeIntToFile(const char * path, int value);
    int writeInt64ToFile(const char * path, int64_t value);

    int writeReadIntInFile(const char * path, int & value);
    int writeReadInt64InFile(const char * path, int64_t & value);

    std::set<int> readCpuMaskFromFile(const char * path);

    uint64_t roundDownToPowerOfTwo(uint64_t in);
    int calculatePerfMmapSizeInPages(const std::uint64_t perfEventMlockKb, const std::uint64_t pageSizeBytes);

    /**
    * @returns true if current uid is for Root or Android Shell, false otherwise
    */
    bool isRootOrShell();

    template<typename... T>
    struct always_false : std::false_type {
    };

}

#endif // INCLUDE_LIB_UTILS_H
