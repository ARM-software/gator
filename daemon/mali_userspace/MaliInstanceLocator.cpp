/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliInstanceLocator.h"
#include "lib/FsEntry.h"
#include "lib/Optional.h"

#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <sstream>

#include "DynBuf.h"
#include "Logging.h"

namespace mali_userspace
{
    static std::unique_ptr<MaliDevice> enumerateMaliHwCntrDriver(const char * model, int mpNumber, int rValue, int pValue, int gpuId, std::string devicePath, std::string  clockPath)
    {
        auto device = MaliDevice::create(gpuId, devicePath, clockPath) ;

        if (device != nullptr) {
            logg.logSetup("Mali Hardware Counters\n'%s MP%d r%dp%d 0x%04X' @ '%s' found", model, mpNumber, rValue, pValue, gpuId, devicePath.c_str());
        }
        else {
            logg.logSetup("Mali Hardware Counters\n'%s MP%d r%dp%d 0x%04X' @ '%s' found, but not supported", model, mpNumber, rValue, pValue, gpuId, devicePath.c_str());
        }

        return device;
    }

    static void enumerateMaliHwCntrDriversInDir(const lib::FsEntry & sysDevicesPlatformDir, std::map<unsigned int , std::unique_ptr<MaliDevice>>& coreDriverMap)
    {
        // open sysfs directory
        if (sysDevicesPlatformDir.read_stats().type() != lib::FsEntry::Type::DIR) {
            logg.logMessage("Failed to open '%s'", sysDevicesPlatformDir.path().c_str());
            return;
        }

        // walk children looking for <CHILD>/gpuinfo and <CHILD>/misc/mali<%d>
        lib::FsEntryDirectoryIterator iterator = sysDevicesPlatformDir.children();
        lib::Optional<lib::FsEntry> childEntry;
        while ((childEntry = iterator.next()).valid()) {
            // determine type
            lib::FsEntry::Stats childStats = childEntry->read_stats();
            if (childStats.type() == lib::FsEntry::Type::DIR) {
                // try to recursively scan the child directory
                if (!childStats.is_symlink()) {
                    enumerateMaliHwCntrDriversInDir(*childEntry, coreDriverMap);
                }
                // Create the gpuinfo object
                lib::FsEntry gpuinfoEntry = lib::FsEntry::create(*childEntry, "gpuinfo");
                if (!gpuinfoEntry.exists()) {
                    continue;
                }
                // read the contents of 'gpuinfo' file
                std::string data = gpuinfoEntry.readFileContentsSingleLine();
                if (data.empty()) {
                    logg.logMessage("File %s is empty", gpuinfoEntry.path().c_str());
                    continue;
                }
                char model[33];
                int mpNumber ,rValue, pValue;
                unsigned int gpuId;

                int fscanfResult = sscanf(data.c_str(), "%32s MP%d r%dp%d 0x%04x", model, &mpNumber, &rValue, &pValue,
                                          &gpuId);
                if (fscanfResult != 5) {
                    fscanfResult = sscanf(data.c_str(), "(Unknown Mali GPU) MP%d r%dp%d 0x%04x", &mpNumber, &rValue,
                                          &pValue, &gpuId);
                    fscanfResult = (fscanfResult == 4 ? 5 : fscanfResult);
                    std::strcpy(model, "Unknown Mali GPU");

                }
                if (fscanfResult != 5) {
                    fscanfResult = sscanf(data.c_str(), "%32s %d cores r%dp%d 0x%04x", model, &mpNumber, &rValue,
                                          &pValue, &gpuId);
                }
                if (fscanfResult != 5) {
                    fscanfResult = sscanf(data.c_str(), "(Unknown Mali GPU) %d cores r%dp%d 0x%04x", &mpNumber, &rValue,
                                          &pValue, &gpuId);
                    fscanfResult = (fscanfResult == 4 ? 5 : fscanfResult);
                    std::strcpy(model, "Unknown Mali GPU");
                }
                if (fscanfResult != 5) {
                    fscanfResult = sscanf(data.c_str(), "%32s %d core r%dp%d 0x%04x", model, &mpNumber, &rValue, &pValue,
                                          &gpuId);
                }
                if (fscanfResult != 5) {
                    fscanfResult = sscanf(data.c_str(), "(Unknown Mali GPU) %d core r%dp%d 0x%04x", &mpNumber, &rValue,
                                          &pValue, &gpuId);
                    fscanfResult = (fscanfResult == 4 ? 5 : fscanfResult);
                    std::strcpy(model, "Unknown Mali GPU");
                }
                if (fscanfResult != 5) {
                    logg.logError("Failed to parse gpuinfo -> '%s'", data.c_str());
                    continue;
                }
                logg.logMessage("parsed gpu info %s' with '%s MP%d r%dp%d 0x%04X'", gpuinfoEntry.path().c_str(), model, mpNumber, rValue, pValue, gpuId);
                // now check for <CHILD>/misc/mali<%d> directories
                lib::FsEntry miscDirEntry = lib::FsEntry::create(*childEntry, "misc");
                if (miscDirEntry.read_stats().type() != lib::FsEntry::Type::DIR) {
                    logg.logError("could not open '%s'", miscDirEntry.path().c_str());
                    continue;
                }
                lib::Optional<lib::FsEntry> clockPath;
                lib::Optional<lib::FsEntry> devicePath;
                lib::Optional<lib::FsEntry> miscChildEntry;
                lib::FsEntryDirectoryIterator miscIterator = miscDirEntry.children();
                while ((miscChildEntry = miscIterator.next()).valid()) {
                    // match child against 'mali%d'
                    int id = -1;
                    if (sscanf(miscChildEntry->name().c_str(), "mali%d", &id) == 1) {
                        if (coreDriverMap.count(id) > 0) { //already added
                            logg.logMessage("Device already created for '%s' %d", miscChildEntry->name().c_str(), gpuId);
                            continue;
                        }
                        devicePath = lib::FsEntry::create(lib::FsEntry::create("/dev"), miscChildEntry->name());
                        clockPath = lib::FsEntry::create(*miscChildEntry, "clock");
                        if (!devicePath.get().exists()) {
                            logg.logError("could not find %s/mail<N>", miscDirEntry.path().c_str());
                            continue;
                        }
                        if (!clockPath.get().exists() || (!clockPath->canAccess(true, false, false))) {
                            clockPath = lib::FsEntry::create(*childEntry, "clock");
                        }
                        // create the device object
                        auto result = enumerateMaliHwCntrDriver(model, mpNumber, rValue, pValue, gpuId,
                                                           devicePath->path(),
                                                           clockPath->path());
                        if (result != nullptr) {
                            coreDriverMap[id] = std::move(result);
                        } else {
                            logg.logError("Did not recognise Mali product name '%s'%d", devicePath->path().c_str(), gpuId);
                        }
                    }
                    else {
                        logg.logError("not a valid mali gpu %s ", miscDirEntry.path().c_str());
                    }
                }
            }
        }
    }

    static bool addMaliCounterDriver(std::string userSpecifiedDevicePath, std::string userSpecifiedDeviceType, std::map<unsigned int , std::unique_ptr<MaliDevice> >& coreDriverMap) {

        lib::FsEntry deviceFsEntry = lib::FsEntry::create(userSpecifiedDevicePath);
        if (!deviceFsEntry.exists() || !deviceFsEntry.canAccess(true, true, false)) {
            logg.logError("Cannot access mali device path '%s'", userSpecifiedDevicePath.c_str());
            return false;
        }
        else {
            // try to find a gpuID by name
            uint32_t gpuId = MaliDevice::findProductByName(userSpecifiedDeviceType.c_str());
            // then by hex code
            if (gpuId == 0) {
                gpuId = strtoul(userSpecifiedDeviceType.c_str(), nullptr, 0);
            }
            int id = -1;
            std::string path(userSpecifiedDevicePath);
            //gets the last number in the path ?
            size_t last_char_pos = path.find_last_not_of("0123456789");
            if (last_char_pos == std::string::npos) {
                id = 0;
            } else {
                std::string base = path.substr(last_char_pos + 1,path.size());
                if(sscanf(base.c_str(), "%d", &id) == -1) {
                    id = 0;
                }
            }
            if(coreDriverMap.find(id) == coreDriverMap.end()) { //not already added
                auto  result = MaliDevice::create(gpuId, userSpecifiedDevicePath, "");
                if (result == nullptr) {
                    logg.logError("Did not recognise Mali product name '%s' 0x%04x", userSpecifiedDeviceType.c_str(), gpuId);
                    return false;
                }
                coreDriverMap[id] = std::move(result);
                logg.logSetup("Mali Hardware Counters\nUsing user provided Arm Mali GPU driver for Mali is %s at %s", coreDriverMap[id]-> getProductName(), userSpecifiedDevicePath.c_str());
                return true;
            } else {
                logg.logError("Device already created for '%s' %d", userSpecifiedDevicePath.c_str(), gpuId);
                return false;
            }
        }
    }

    std::map<unsigned int, std::unique_ptr<MaliDevice>> enumerateAllMaliHwCntrDrivers(
            std::vector<std::string> userSpecifiedDeviceTypes, std::vector<std::string> userSpecifiedDevicePaths)
    {
        std::map<unsigned int , std::unique_ptr<MaliDevice>>  coreDriverMap;
        // scan first as scan always overrides user settings
        enumerateMaliHwCntrDriversInDir(lib::FsEntry::create("/sys"), coreDriverMap);
        if (!coreDriverMap.empty()) {
            if (!userSpecifiedDeviceTypes.empty()) {
                logg.logError("Ignoring user provided Mali device type");
            }
            return coreDriverMap;
        }
        // try user provided value, which adds only one entry to map as it has key as gpuid derived from userSpecifiedDeviceType
        if (!userSpecifiedDeviceTypes.empty()) {
            // validate path first
            if (userSpecifiedDevicePaths.empty()) {
                //default paths will be used for each specified type.
                for (unsigned int i = 0; i < userSpecifiedDeviceTypes.size(); ++i) {
                    //construct the path
                    std::ostringstream stream;
                    stream << "/dev/mali" << i;
                    std::string path = stream.str();
                    //add counter
                    addMaliCounterDriver(path, userSpecifiedDeviceTypes[i], coreDriverMap);
                }
            }
            else {
                //iterate through the number of user specified paths.
                for (unsigned int i = 0; i < userSpecifiedDevicePaths.size(); ++i) {
                    //if only one type is specified then all paths of of this type.
                    if (userSpecifiedDeviceTypes.size() == 1) {
                        addMaliCounterDriver(userSpecifiedDevicePaths[i], userSpecifiedDeviceTypes[0],
                                             coreDriverMap);
                    }
                    else if (i < userSpecifiedDeviceTypes.size()) {
                        //This must mean that each type has a corresponding path.
                        addMaliCounterDriver(userSpecifiedDevicePaths[i], userSpecifiedDeviceTypes[i],
                                             coreDriverMap);
                    }
                }
            }
        }
        return coreDriverMap;
    }
}
