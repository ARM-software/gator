/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#include <map>
#include <string>

namespace lib {
    class FsEntry;
}

namespace lnx {
    /**
     * Utility to track default stuff from sysfs
     */
    void addDefaultSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes);

    /**
     * Add summary information from sysfs (or any folder) for some file
     */
    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const std::string & deviceName,
                                    const std::string & dataDirName,
                                    const lib::FsEntry & dataFile);

    /**
     * Add summary information from sysfs (or any folder) for all files in a directory
     */
    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const lib::FsEntry & deviceDirectory,
                                    const std::string & dataDirName);
}
