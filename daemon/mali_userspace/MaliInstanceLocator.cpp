/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#include "mali_userspace/MaliInstanceLocator.h"
#include "lib/FsEntry.h"
#include "lib/Optional.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "DynBuf.h"
#include "Logging.h"

namespace mali_userspace
{
    static MaliDevice * enumerateMaliHwCntrDriver(const char * model, int mpNumber, int rValue, int pValue, int gpuId, const char * devicePath, const char * /*clockPath*/)
    {
        // [SDDAP-8262] Disabled clock counter as currently mali hardware counters lock Mali clock frequency rendering the counter useless
        MaliDevice * device = MaliDevice::create(mpNumber, gpuId, devicePath, /*clockPath*/ nullptr);

        if (device != NULL) {
            logg.logSetup("Mali Hardware Counters\n'%s MP%d r%dp%d 0x%04X' @ '%s' found", model, mpNumber, rValue, pValue, gpuId, devicePath);
        }
        else {
            logg.logSetup("Mali Hardware Counters\n'%s MP%d r%dp%d 0x%04X' @ '%s' found, but not supported", model, mpNumber, rValue, pValue, gpuId, devicePath);
        }

        return device;
    }

    static MaliDevice * enumerateMaliHwCntrDriversInDir(const lib::FsEntry & sysDevicesPlatformDir)
    {
        // open sysfs directory
        if (sysDevicesPlatformDir.read_stats().type() != lib::FsEntry::Type::DIR) {
            logg.logMessage("enumerateMaliHwCntrDrivers - failed to open '%s'", sysDevicesPlatformDir.path().c_str());
            return NULL;
        }

        MaliDevice * result = NULL;

        // walk children looking for <CHILD>/gpuinfo and <CHILD>/misc/mali<%d>
        lib::FsEntryDirectoryIterator iterator = sysDevicesPlatformDir.children();
        lib::Optional<lib::FsEntry> childEntry;
        while ((childEntry = iterator.next()).valid()) {
            // determine type
            lib::FsEntry::Stats childStats = childEntry->read_stats();
            if (childStats.type() == lib::FsEntry::Type::DIR) {
                // try to recursively scan the child directory
                if (!childStats.is_symlink()) {
                    result = enumerateMaliHwCntrDriversInDir(*childEntry);
                    if (result != NULL) {
                        return result;
                    }
                }

                // Create the gpuinfo object
                lib::FsEntry gpuinfoEntry = lib::FsEntry::create(*childEntry, "gpuinfo");

                // read the contents of 'gpuinfo' file
                FILE * gpuinfoFile = fopen(gpuinfoEntry.path().c_str(), "r");
                if (gpuinfoFile == NULL) {
                    continue;
                }

                char model[33];
                int mpNumber, rValue, pValue;
                unsigned int gpuId;
                int fscanfResult = fscanf(gpuinfoFile, "%32s MP%d r%dp%d 0x%04X", model, &mpNumber, &rValue, &pValue, &gpuId);
                if (fscanfResult != 5) {
                    fseek(gpuinfoFile, 0, SEEK_SET);
                    fscanfResult = fscanf(gpuinfoFile, "%32s %d cores r%dp%d 0x%04X", model, &mpNumber, &rValue, &pValue, &gpuId);
                    if (fscanfResult != 5) {
                        fseek(gpuinfoFile, 0, SEEK_SET);
                        fscanfResult = fscanf(gpuinfoFile, "%32s %d core r%dp%d 0x%04X", model, &mpNumber, &rValue, &pValue, &gpuId);

                        fclose(gpuinfoFile);

                        if (fscanfResult != 5) {
                            logg.logError("enumerateMaliHwCntrDrivers - failed to parse '%s'", gpuinfoEntry.path().c_str());
                            continue;
                        }
                    }
                }

                logg.logMessage("enumerateMaliHwCntrDrivers - Detected valid gpuinfo file '%s' with '%s MP%d r%dp%d 0x%04X'",
                                gpuinfoEntry.path().c_str(), model, mpNumber, rValue, pValue, gpuId);

                // now check for <CHILD>/misc/mali<%d> directories
                lib::FsEntry miscDirEntry = lib::FsEntry::create(*childEntry, "misc");

                if (miscDirEntry.read_stats().type() != lib::FsEntry::Type::DIR) {
                    logg.logError("enumerateMaliHwCntrDrivers - could not open '%s'", miscDirEntry.path().c_str());
                    continue;
                }

                lib::Optional<lib::FsEntry> clockPath;
                lib::Optional<lib::FsEntry> devicePath;
                lib::Optional<lib::FsEntry> miscChildEntry;
                lib::FsEntryDirectoryIterator miscIterator = miscDirEntry.children();
                while ((!devicePath.valid()) && (miscChildEntry = miscIterator.next()).valid()) {
                    // match child against 'mali%d'
                    int ignored;
                    if (sscanf(miscChildEntry->name().c_str(), "mali%d", &ignored) == 1) {
                        devicePath = lib::FsEntry::create(lib::FsEntry::create("/dev"), miscChildEntry->name());
                        clockPath = lib::FsEntry::create(*miscChildEntry, "clock");
                    }
                    else {
                        logg.logMessage("enumerateMaliHwCntrDrivers - skipping '%s'", miscChildEntry->path().c_str());
                    }
                }

                if (!devicePath.valid()) {
                    logg.logError("enumerateMaliHwCntrDrivers - could not find %s/mail<N>", miscDirEntry.path().c_str());
                    continue;
                }

                if ((!clockPath.valid()) || (!clockPath->canAccess(true, false, false))) {
                    clockPath = lib::FsEntry::create(*childEntry, "clock");
                }

                // create the device object
                result = enumerateMaliHwCntrDriver(model, mpNumber, rValue, pValue, gpuId, devicePath->path().c_str(), (clockPath.valid() ? clockPath->path().c_str() : nullptr));
                if (result != NULL) {
                    return result;
                }
            }
        }

        return NULL;
    }

    MaliDevice * enumerateMaliHwCntrDrivers()
    {
        MaliDevice * result = enumerateMaliHwCntrDriversInDir(lib::FsEntry::create("/sys"));
        if (result != NULL) {
            return result;
        }

        return NULL;
    }
}
