/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "linux/SysfsSummaryInformation.h"

#include "Logging.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"

#include <cstring>

namespace lnx {
    void addDefaultSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes)
    {
        // read SPE caps files
        {
            lib::FsEntry sysBusEventSourceDevices = lib::FsEntry::create("/sys/bus/event_source/devices");
            lib::FsEntryDirectoryIterator deviceDirIterator = sysBusEventSourceDevices.children();
            while (lib::Optional<lib::FsEntry> deviceDir = deviceDirIterator.next()) {
                std::string deviceName = deviceDir->name();
                // send metadata about perf devices
                addSysfsSummaryInformation(additionalAttributes,
                                           "perf.devices",
                                           deviceName,
                                           "",
                                           lib::FsEntry::create(*deviceDir, "type"));
                // and there capabilities
                addSysfsSummaryInformation(additionalAttributes, "perf.devices", *deviceDir, "caps");
            }
        }
    }

    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const std::string & deviceName,
                                    const std::string & dataDirName,
                                    const lib::FsEntry & dataFile)
    {
        if (dataFile.read_stats().type() == lib::FsEntry::Type::FILE) {
            const std::string contents = dataFile.readFileContentsSingleLine();
            const std::string key =
                (dataDirName.empty()
                     ? lib::Format() << prefix << "." << deviceName << "." << dataFile.name()
                     : lib::Format() << prefix << "." << deviceName << "." << dataDirName << "." << dataFile.name());

            logg.logMessage("Read summary metadata item '%s' = '%s'", key.c_str(), contents.c_str());
            additionalAttributes[key] = contents;
        }
    }

    void addSysfsSummaryInformation(std::map<std::string, std::string> & additionalAttributes,
                                    const char * prefix,
                                    const lib::FsEntry & deviceDirectory,
                                    const std::string & dataDirName)
    {
        std::string deviceName = deviceDirectory.name();
        lib::FsEntry dataDir = lib::FsEntry::create(deviceDirectory, dataDirName);
        lib::FsEntryDirectoryIterator dataDirIterator = dataDir.children();

        while (lib::Optional<lib::FsEntry> dataFile = dataDirIterator.next()) {
            addSysfsSummaryInformation(additionalAttributes, prefix, deviceName, dataDirName, *dataFile);
        }
    }
}
