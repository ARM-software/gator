/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_UTILS_H
#define INCLUDE_LIB_UTILS_H

#include <cstdint>
#include <optional>
#include <set>

#include <linux/version.h>
#include <sys/types.h>
#include <sys/utsname.h>

namespace lib {

    using kernel_version_no_t = unsigned;

    kernel_version_no_t parseLinuxVersion(struct utsname & utsname);

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

    /**
    * @brief  gets the UID and GID for a certain user
    *
    */
    std::optional<std::pair<uid_t, gid_t>> resolve_uid_gid(char const * username);

    /** Takes any type and evaluates to false.
     *
     * This is used in static_asserts that you always want to fail in templated contexts, as you can't just put false
     * in as the compiler will always trigger it even if wrapped in a if constexpr (false).  So you use this with an
     * input parameter type to force a conditional evaluation.
     */
    template<typename... T>
    struct always_false : std::false_type {
    };

    /** Use in a std::visit to allow for multiple Callables to handle variant types based on overloading.
     *
     * This is pretty much a copy of the overloaded type used in the example in
     * in https://en.cppreference.com/w/cpp/utility/variant/visit.
     */
    template<class... Ts>
    struct visitor_overloader : Ts... {
        explicit visitor_overloader(Ts &&... ts) : Ts {std::forward<Ts>(ts)}... {}

        using Ts::operator()...;
    };
}

#endif // INCLUDE_LIB_UTILS_H
