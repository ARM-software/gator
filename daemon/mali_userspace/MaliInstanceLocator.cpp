/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#include "mali_userspace/MaliInstanceLocator.h"

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "DynBuf.h"
#include "Logging.h"

#define SYS_DEVICES_PLATFORM_DIR "/sys/devices/platform"
#define SYS_DEVICES_DIR "/sys/devices/"

namespace mali_userspace
{
    static MaliDevice * enumerateMaliHwCntrDriver(const char * model, int mpNumber, int rValue, int pValue, int gpuId, const char * devicePath)
    {
        MaliDevice * device = MaliDevice::create(mpNumber, gpuId, devicePath);

        if (device != NULL) {
            logg.logSetup("Mali Hardware Counters - '%s MP%d r%dp%d 0x%04X' @ '%s' found", model, mpNumber, rValue, pValue, gpuId, devicePath);
        }
        else {
            logg.logSetup("Mali Hardware Counters - '%s MP%d r%dp%d 0x%04X' @ '%s' found, but not supported", model, mpNumber, rValue, pValue, gpuId, devicePath);
        }

        return device;
    }

    static MaliDevice * enumerateMaliHwCntrDriversInDir(const char * dir)
    {
        // open sysfs directory
        DIR * sysDevicesPlatformDir = opendir(dir);
        if (sysDevicesPlatformDir == NULL) {
            logg.logMessage("enumerateMaliHwCntrDrivers - failed to open '%s'", dir);
            return NULL;
        }

        MaliDevice * result = NULL;

        // walk children looking for <CHILD>/gpuinfo and <CHILD>/misc/mali<%d>
        struct dirent * deviceDirent;
        while ((result == NULL) && ((deviceDirent = readdir(sysDevicesPlatformDir)) != NULL)) {
            // create the <CHILD> string
            DynBuf gpuinfoPathBuffer;
            gpuinfoPathBuffer.append("%s/%s/gpuinfo", dir, deviceDirent->d_name);

            // read the contents of 'gpuinfo' file
            FILE * gpuinfoFile = fopen(gpuinfoPathBuffer.getBuf(), "r");
            if (gpuinfoFile == NULL) {
                continue;
            }

            char model[33];
            int mpNumber, rValue, pValue, gpuId;
            int fscanfResult = fscanf(gpuinfoFile, "%32s MP%d r%dp%d 0x%04X", model, &mpNumber, &rValue, &pValue, &gpuId);

            fclose(gpuinfoFile);

            if (fscanfResult != 5) {
                logg.logError("enumerateMaliHwCntrDrivers - failed to parse '%s'", gpuinfoPathBuffer.getBuf());
                continue;
            }

            logg.logMessage("enumerateMaliHwCntrDrivers - Detected valid gpuinfo file '%s' with '%s MP%d r%dp%d 0x%04X'",
                    gpuinfoPathBuffer.getBuf(), model, mpNumber, rValue, pValue, gpuId);

            // now check for <CHILD>/misc/mali<%d> directories
            DynBuf miscPathBuffer;
            miscPathBuffer.append("%s/%s/misc", dir, deviceDirent->d_name);

            DIR * miscDir = opendir(miscPathBuffer.getBuf());
            if (miscDir == NULL) {
                logg.logError("enumerateMaliHwCntrDrivers - could not open '%s'", miscPathBuffer.getBuf());
                continue;
            }

            bool matched = false;
            DynBuf maliDeviceName;
            struct dirent * miscDirent;
            while ((!matched) && ((miscDirent = readdir(miscDir)) != NULL)) {
                // match child against 'mali%d'
                int ignored;
                if (sscanf(miscDirent->d_name, "mali%d", &ignored) == 1) {
                    matched = true;
                    maliDeviceName.append("/dev/%s", miscDirent->d_name);
                }
                // log a message so long as name is not '.' or '..'
                else if ((miscDirent->d_name[0] != '.')
                        || ((miscDirent->d_name[1] != '\0') && ((miscDirent->d_name[1] != '.') || (miscDirent->d_name[2] != '\0')))) {
                    logg.logMessage("enumerateMaliHwCntrDrivers - skipping '%s/%s'", miscPathBuffer.getBuf(), miscDirent->d_name);
                }
            }

            closedir(miscDir);

            if (!matched) {
                logg.logError("enumerateMaliHwCntrDrivers - could not find %s/mail<N>", miscPathBuffer.getBuf());
                continue;
            }

            // create the device object
            result = enumerateMaliHwCntrDriver(model, mpNumber, rValue, pValue, gpuId, maliDeviceName.getBuf());
        }

        closedir(sysDevicesPlatformDir);

        return result;
    }

    MaliDevice * enumerateMaliHwCntrDrivers()
    {
        MaliDevice * result = enumerateMaliHwCntrDriversInDir(SYS_DEVICES_PLATFORM_DIR);
        if (result != NULL) {
            return result;
        }

        result = enumerateMaliHwCntrDriversInDir(SYS_DEVICES_DIR);
        if (result != NULL) {
            return result;
        }

        return NULL;
    }
}
