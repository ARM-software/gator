/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <map>
#include <string>

namespace lib {
    class FsEntry;
}

namespace lnx
{
    /**
     * Utility to track default stuff from sysfs
     */
    void addDefaultSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes);

    /**
     * Add summary information from sysfs (or any folder) for some file
     *
     * @param additionalAttributes
     * @param prefix
     * @param deviceName
     * @param dataDirName
     * @param dataFile
     */
    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const std::string & deviceName,
                                    const std::string & dataDirName,
                                    const lib::FsEntry & dataFile);

    /**
     * Add summary information from sysfs (or any folder) for all files in a directory
     *
     * @param additionalAttributes
     * @param prefix
     * @param deviceDirectory
     * @param dataDirName
     */
    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const lib::FsEntry & deviceDirectory,
                                    const std::string & dataDirName);
}
