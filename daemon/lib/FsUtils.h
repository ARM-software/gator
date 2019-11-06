/**
 * Copyright (C) Arm Limited 2019. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LIB_FS_UTILS_H
#define LIB_FS_UTILS_H

#include <set>
#include <ios>
#include <sstream>

#include "lib/FsEntry.h"

namespace lib
{
    /**
     * Gets all the directory entries that have numerical names
     *
     * @param parent the directory whose entries to check
     * @return the set of entries that name can be parsed exactly (no whitespace or other characters)
     */
    template<typename IntType>
    std::set<IntType> getNumericalDirectoryEntries(const char *parent)
    {
        std::set<IntType> numbers;
        auto iterator = FsEntry::create(parent).children();

        Optional<FsEntry> childEntry;
        while ((childEntry = iterator.next()).valid()) {
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
