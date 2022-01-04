/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef LIB_FS_UTILS_H
#define LIB_FS_UTILS_H

#include "lib/FsEntry.h"

#include <ios>
#include <set>
#include <sstream>

namespace lib {
    /**
     * Gets all the directory entries that have numerical names
     *
     * @param parent the directory whose entries to check
     * @return the set of entries that name can be parsed exactly (no whitespace or other characters)
     */
    template<typename IntType>
    std::set<IntType> getNumericalDirectoryEntries(const char * parent)
    {
        std::set<IntType> numbers;
        auto iterator = FsEntry::create(parent).children();

        std::optional<FsEntry> childEntry;
        while (!!(childEntry = iterator.next())) {
            auto nameStream = std::stringstream(childEntry->name());

            IntType value;
            nameStream >> std::noskipws >> value;

            if (!nameStream.fail() && nameStream.eof()) {
                numbers.insert(value);
            }
        }
        return numbers;
    }

}

#endif /* LIB_FS_UTILS_H */
