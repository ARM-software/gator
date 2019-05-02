/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "CpuUtils.h"

#include <dirent.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <unistd.h>

#include "CpuUtils_Topology.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "lib/File.h"
#include "linux/PerCoreIdentificationThread.h"

namespace cpu_utils
{
    unsigned int getMaxCoreNum()
    {
        // why don't we just use /sys/devices/system/cpu/kernel_max
        // or pick the highest in /sys/devices/system/cpu/possible?
        DIR *dir = opendir("/sys/devices/system/cpu");
        if (dir == NULL) {
            logg.logError("Unable to determine the number of cores on the target, opendir failed");
            handleException();
        }

        long maxCoreNum = -1;
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            if (strncmp(dirent->d_name, "cpu", 3) == 0) {
                long coreNum;
                if (stringToLong(&coreNum, dirent->d_name + 3, 10) && (coreNum >= maxCoreNum)) {
                    maxCoreNum = coreNum + 1;
                }
            }
        }
        closedir(dir);

        if (maxCoreNum < 1) {
            logg.logError("Unable to determine the number of cores on the target, no cpu# directories found");
            handleException();
        }

        return maxCoreNum;
    }

    static void setImplementer(int & cpuId, const int implementer)
    {
        if (cpuId == -1) {
            cpuId = 0;
        }
        cpuId |= implementer << 12;
    }

    static void setPart(int & cpuId, const int part)
    {
        if (cpuId == -1) {
            cpuId = 0;
        }
        cpuId |= part;
    }

    static const char HARDWARE[] = "Hardware";
    static const char CPU_IMPLEMENTER[] = "CPU implementer";
    static const char CPU_PART[] = "CPU part";
    static const char PROCESSOR[] = "processor";

    static inline constexpr unsigned makeCpuId(std::uint64_t midr)
    {
        return ((midr & 0xff000000) >> 12) | ((midr & 0xfff0) >> 4);
    }

    static std::string parseProcCpuInfo(bool justGetHardwareName, lib::Span<int> cpuIds)
    {
        std::string hardwareName;
        char temp[256]; // arbitrarily large amount

        FILE *f = lib::fopen_cloexec("/proc/cpuinfo", "r");
        if (f == NULL) {
            logg.logMessage("Error opening /proc/cpuinfo\n"
                    "The core name in the captured xml file will be 'unknown'.");
            return hardwareName;
        }

        bool foundCoreName = false;
        constexpr size_t UNKNOWN_PROCESSOR = -1;
        size_t processor = UNKNOWN_PROCESSOR;
        size_t minProcessor = cpuIds.length;
        size_t maxProcessor = 0;
        bool foundProcessorInSection = false;
        int outOfPlaceCpuId = -1;
        bool invalidFormat = false;
        while (fgets(temp, sizeof(temp), f)) {
            const size_t len = strlen(temp);

            if (len > 0) {
                // Replace the line feed with a null
                temp[len - 1] = '\0';
            }

            logg.logMessage("cpuinfo: %s", temp);

            if (len == 1) {
                // New section, clear the processor. Streamline will not know the cpus if the pre Linux 3.8 format of cpuinfo is encountered but also that no incorrect information will be transmitted.
                processor = UNKNOWN_PROCESSOR;
                foundProcessorInSection = false;
                continue;
            }

            const bool foundHardware = !foundCoreName && strncmp(temp, HARDWARE, sizeof(HARDWARE) - 1) == 0;
            const bool foundCPUImplementer = strncmp(temp, CPU_IMPLEMENTER, sizeof(CPU_IMPLEMENTER) - 1) == 0;
            const bool foundCPUPart = strncmp(temp, CPU_PART, sizeof(CPU_PART) - 1) == 0;
            const bool foundProcessor = strncmp(temp, PROCESSOR, sizeof(PROCESSOR) - 1) == 0;
            if (foundHardware || foundCPUImplementer || foundCPUPart || foundProcessor) {
                char* position = strchr(temp, ':');
                if (position == NULL || static_cast<unsigned int>(position - temp) + 2 >= strlen(temp)) {
                    logg.logMessage("Unknown format of /proc/cpuinfo\n"
                            "The core name in the captured xml file will be 'unknown'.");
                    return hardwareName;
                }
                position += 2;

                if (foundHardware) {
                    hardwareName = position;
                    if (justGetHardwareName) {
                        return hardwareName;
                    }
                    foundCoreName = true;
                }

                if (foundCPUImplementer) {
                    int implementer;
                    if (!stringToInt(&implementer, position, 0)) {
                        // Do nothing
                    }
                    else if (processor != UNKNOWN_PROCESSOR) {
                        setImplementer(cpuIds[processor], implementer);
                    }
                    else {
                        setImplementer(outOfPlaceCpuId, implementer);
                        invalidFormat = true;
                    }
                }

                if (foundCPUPart) {
                    int cpuId;
                    if (!stringToInt(&cpuId, position, 0)) {
                        // Do nothing
                    }
                    else if (processor != UNKNOWN_PROCESSOR) {
                        setPart(cpuIds[processor], cpuId);
                    }
                    else {
                        setPart(outOfPlaceCpuId, cpuId);
                        invalidFormat = true;
                    }
                }

                if (foundProcessor) {
                    int processorId = -1;
                    const bool converted = stringToInt(&processorId, position, 0);

                    // update min and max processor ids
                    if (converted) {
                        minProcessor = (static_cast<size_t>(processorId) < minProcessor ? processorId : minProcessor);
                        maxProcessor = (static_cast<size_t>(processorId) > maxProcessor ? processorId : maxProcessor);
                    }

                    if (foundProcessorInSection) {
                        // Found a second processor in this section, ignore them all
                        processor = UNKNOWN_PROCESSOR;
                        invalidFormat = true;
                    }
                    else if (converted) {
                        processor = processorId;
                        if (processor >= cpuIds.length) {
                            logg.logError("Found processor %zu but max is %zu", processor, cpuIds.length);
                            handleException();
                        }
                        foundProcessorInSection = true;
                    }
                }
            }
        }
        fclose(f);

        if (invalidFormat && (outOfPlaceCpuId != -1) && (minProcessor <= maxProcessor)) {
            minProcessor = (minProcessor > 0 ? minProcessor : 0);
            maxProcessor = (maxProcessor < cpuIds.length ? maxProcessor + 1 : cpuIds.length);

            for (size_t processor = minProcessor; processor < maxProcessor; ++processor) {
                if (cpuIds[processor] == -1) {
                    logg.logMessage("Setting global CPUID 0x%x for processors %zu ", outOfPlaceCpuId, processor);
                    cpuIds[processor] = outOfPlaceCpuId;
                }
            }
        }

        if (!foundCoreName) {
            logg.logMessage("Could not determine core name from /proc/cpuinfo\n"
                    "The core name in the captured xml file will be 'unknown'.");
        }

        return hardwareName;
    }

    std::string readCpuInfo(bool ignoreOffline, lib::Span<int> cpuIds)
    {
        std::mutex mutex;
        unsigned identificationThreadCallbackCounter = 0;
        std::condition_variable cv;
        std::map<unsigned, unsigned> cpuToCluster;
        std::map<unsigned, std::set<unsigned>> clusterToCpuIds;
        std::map<unsigned, unsigned> cpuToCpuIds;
        std::vector<std::unique_ptr<PerCoreIdentificationThread>> perCoreThreads;

        // wake all cores; this ensures the contents of /proc/cpuinfo reflect the full range of cores in the system.
        // this works as follows:
        // - spawn one thread per core that is affined to each core
        // - once all cores are online and affined, *and* have read the data they are required to read, then they callback here to notify this method to continue
        // - the threads remain online until this function finishes (they are disposed of / terminated by destructor); this is so as
        //   to ensure that the cores remain online until cpuinfo is read
        {
            for (unsigned cpu = 0; cpu < cpuIds.length; ++cpu) {
                perCoreThreads.emplace_back(
                        new PerCoreIdentificationThread(
                                ignoreOffline, cpu,
                                [&] (unsigned c, unsigned core_id, unsigned physical_package_id, std::set<int> core_siblings, std::uint64_t midr_el1) -> void
                                {
                                    std::lock_guard<std::mutex> guard {mutex};

                                    // update completed count
                                    identificationThreadCallbackCounter += 1;
                                    cv.notify_one();

                                    const unsigned cpuId = makeCpuId(midr_el1);

                                    // store the cluster / core mappings to allow us to fill in any gaps by assuming the same core type per cluster
                                    if (physical_package_id != PerCoreIdentificationThread::INVALID_PACKAGE_ID) {
                                        cpuToCluster[c] = physical_package_id;

                                        // also map cluster to MIDR value if read
                                        if (midr_el1 != PerCoreIdentificationThread::INVALID_MIDR_EL1) {
                                            clusterToCpuIds[physical_package_id].insert(cpuId);
                                        }

                                        for (int sibling : core_siblings) {
                                            const unsigned sibling_cpu = sibling;

                                            if (cpuToCluster.count(sibling_cpu) == 0) {
                                                cpuToCluster[sibling_cpu] = physical_package_id;
                                            }
                                        }
                                    }

                                    // map cpu to MIDR value if read
                                    if (midr_el1 != PerCoreIdentificationThread::INVALID_MIDR_EL1) {
                                        cpuToCpuIds[c] = cpuId;
                                    }
                                }));
            }

            // wait until all threads are online
            std::unique_lock<std::mutex> lock {mutex};
            cv.wait_for(lock, std::chrono::seconds(10), [&] {
                return identificationThreadCallbackCounter >= cpuIds.length;
            });
        }

        // lock to prevent concurrent access to maps
        std::lock_guard<std::mutex> lock (mutex);

        // log what we learnt
        for (const auto & pair : cpuToCpuIds) {
            logg.logMessage("Read CPU %u CPUID from MIDR_EL1 -> 0x%05x", pair.first, pair.second);
        }
        for (const auto & pair : cpuToCluster) {
            logg.logMessage("Read CPU %u CLUSTER %u", pair.first, pair.second);
        }
        for (const auto & pair : clusterToCpuIds) {
            logg.logMessage("Read CLUSTER %u CPUIDs:", pair.first);
            for (auto cpuId : pair.second) {
                logg.logMessage("    0x%05x", cpuId);
            }
        }

        // did we successfully read all MIDR values from all cores?
        const bool knowAllMidrValues = (cpuToCpuIds.size() == cpuIds.length);

        // do we need to read /proc/cpuinfo
        std::string hardwareName = parseProcCpuInfo(/* justGetHardwareName = */ knowAllMidrValues, cpuIds);

        // update/set known items from MIDR map and topology information. This will override anything read from /proc/cpuinfo
        updateCpuIdsFromTopologyInformation(cpuIds, cpuToCpuIds, cpuToCluster, clusterToCpuIds);

        return hardwareName;
    }
}



