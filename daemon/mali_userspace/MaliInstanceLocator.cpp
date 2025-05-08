/* Copyright (C) 2016-2024 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliInstanceLocator.h"

#include "Logging.h"
#include "device/handle.hpp"
#include "lib/FsEntry.h"
#include "mali_userspace/MaliDevice.h"

#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {
    void enumerateMaliGpuClockPaths(const lib::FsEntry & currentDirectory,
                                    std::map<unsigned int, std::string> & gpuClockPaths)
    {
        // open sysfs directory
        if (currentDirectory.read_stats().type() != lib::FsEntry::Type::DIR) {
            LOG_WARNING("Failed to open '%s'", currentDirectory.path().c_str());
            return;
        }

        // is the parent called 'misc'
        const bool dirIsCalledMisc = (currentDirectory.name() == "misc");
        const std::optional<lib::FsEntry> dirsParent = currentDirectory.parent();
        const std::optional<lib::FsEntry> parentClockPath =
            (dirsParent ? std::optional<lib::FsEntry> {lib::FsEntry::create(*dirsParent, "clock")}
                        : std::optional<lib::FsEntry> {});

        // walk children looking for directories named 'mali%d'
        lib::FsEntryDirectoryIterator iterator = currentDirectory.children();
        std::optional<lib::FsEntry> childEntry;
        while (!!(childEntry = iterator.next())) {
            // determine type
            lib::FsEntry::Stats childStats = childEntry->read_stats();
            if (childStats.type() == lib::FsEntry::Type::DIR) {
                // check name is 'mali#'
                unsigned int id = 0;
                // NOLINTNEXTLINE(cert-err34-c)
                if (dirIsCalledMisc && sscanf(childEntry->name().c_str(), "mali%u", &id) == 1) {
                    // don't repeat your self
                    if (gpuClockPaths.count(id) > 0) {
                        continue;
                    }
                    // check for 'misc/clock' directory
                    lib::FsEntry childClockPath = lib::FsEntry::create(*childEntry, "clock");
                    if (childClockPath.exists() && childClockPath.canAccess(true, false, false)) {
                        gpuClockPaths[id] = childClockPath.path();
                    }
                    // use ../../clock ?
                    else if (parentClockPath && parentClockPath->exists()
                             && parentClockPath->canAccess(true, false, false)) {
                        gpuClockPaths[id] = parentClockPath->path();
                    }
                }
                // try to recursively scan the child directory
                else if (!childStats.is_symlink()) {
                    enumerateMaliGpuClockPaths(*childEntry, gpuClockPaths);
                }
            }
        }
    }
}

namespace mali_userspace {
    std::map<unsigned int, std::unique_ptr<MaliDevice>> enumerateAllMaliHwCntrDrivers()
    {
        using namespace hwcpipe::device;
        static constexpr unsigned int MAX_DEV_MALI_TOO_SCAN_FOR = 16;

        std::map<unsigned int, handle::handle_ptr> detectedDevices;
        std::map<unsigned int, std::string> gpuClockPaths;
        std::map<unsigned int, std::unique_ptr<MaliDevice>> coreDriverMap;

        // first scan for '/dev/mali#' files
        for (unsigned int i = 0; i < MAX_DEV_MALI_TOO_SCAN_FOR; ++i) {
            auto probed_handle = hwcpipe::device::handle::create(i);
            if (probed_handle) {
                LOG_DEBUG("Tried /dev/mali%u success", i);
                detectedDevices[i] = std::move(probed_handle);
            }
            else {
                LOG_DEBUG("Tried /dev/mali%u failed", i);
            }
        }

        LOG_DEBUG("Number of mali files: %zu", detectedDevices.size());

        if (!detectedDevices.empty()) {
            // now scan /sys to find the 'clock' metadata files from which we read gpu frequency
            enumerateMaliGpuClockPaths(lib::FsEntry::create("/sys"), gpuClockPaths);

            // populate result
            for (auto & [id, device] : detectedDevices) {

                auto mali_device = MaliDevice::create(std::move(device), std::move(gpuClockPaths[id]));
                if (mali_device) {
                    coreDriverMap[id] = std::move(mali_device);
                }
            }
        }

        return coreDriverMap;
    }
}
