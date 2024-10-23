/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#include "CpuUtils.h"

#include "CpuUtils_Topology.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "lib/File.h"
#include "lib/Span.h"
#include "linux/PerCoreIdentificationThread.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>

namespace cpu_utils {
    unsigned int getMaxCoreNum()
    {
        // why don't we just use /sys/devices/system/cpu/kernel_max
        // or pick the highest in /sys/devices/system/cpu/possible?
        DIR * dir = opendir("/sys/devices/system/cpu");
        if (dir == nullptr) {
            LOG_ERROR("Unable to determine the number of cores on the target, opendir failed");
            handleException();
        }

        long maxCoreNum = -1;
        struct dirent * dirent;
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        while ((dirent = readdir(dir)) != nullptr) {
            if (strncmp(dirent->d_name, "cpu", 3) == 0) {
                long coreNum;
                if (stringToLong(&coreNum, dirent->d_name + 3, 10) && (coreNum >= maxCoreNum)) {
                    maxCoreNum = coreNum + 1;
                }
            }
        }
        closedir(dir);

        if (maxCoreNum < 1) {
            LOG_ERROR("Unable to determine the number of cores on the target, no cpu# directories found");
            handleException();
        }

        return maxCoreNum;
    }

    namespace {
        constexpr const std::string_view CPU_ARCHITECTURE {"CPU architecture"};
        constexpr const std::string_view CPU_IMPLEMENTER {"CPU implementer"};
        constexpr const std::string_view CPU_PART {"CPU part"};
        constexpr const std::string_view CPU_REVISION {"CPU revision"};
        constexpr const std::string_view CPU_VARIANT {"CPU variant"};
        constexpr const std::string_view HARDWARE {"Hardware"};
        constexpr const std::string_view PROCESSOR {"processor"};

        std::string parseProcCpuInfo(bool justGetHardwareName, lib::Span<midr_t> midrs)
        {
            std::string hardwareName;
            char temp[256]; // NOLINT(modernize-avoid-c-arrays,readability-magic-numbers) - arbitrarily large amount

            FILE * f = lib::fopen_cloexec("/proc/cpuinfo", "r");
            if (f == nullptr) {
                LOG_WARNING("Error opening /proc/cpuinfo\n"
                            "The core name in the captured xml file will be 'unknown'.");
                return hardwareName;
            }

            bool foundCoreName = false;
            constexpr size_t UNKNOWN_PROCESSOR = -1;
            size_t processor = UNKNOWN_PROCESSOR;
            size_t minProcessor = midrs.size();
            size_t maxProcessor = 0;
            bool foundProcessorInSection = false;
            midr_t outOfPlaceCpuId {};
            bool invalidFormat = false;
            while (fgets(temp, sizeof(temp), f) != nullptr) {
                const size_t len = strlen(temp);

                if (len > 0) {
                    // Replace the line feed with a null
                    temp[len - 1] = '\0';
                }

                LOG_DEBUG("cpuinfo: %s", temp);

                if (len == 1) {
                    // New section, clear the processor. Streamline will not know the cpus if the pre Linux 3.8 format of cpuinfo is encountered but also that no incorrect information will be transmitted.
                    processor = UNKNOWN_PROCESSOR;
                    foundProcessorInSection = false;
                    continue;
                }

                const bool foundHardware = !foundCoreName && strncmp(temp, HARDWARE.data(), HARDWARE.size()) == 0;
                const bool foundCPUArchitecture = strncmp(temp, CPU_ARCHITECTURE.data(), CPU_ARCHITECTURE.size()) == 0;
                const bool foundCPUImplementer = strncmp(temp, CPU_IMPLEMENTER.data(), CPU_IMPLEMENTER.size()) == 0;
                const bool foundCPUPart = strncmp(temp, CPU_PART.data(), CPU_PART.size()) == 0;
                const bool foundCPURevision = strncmp(temp, CPU_REVISION.data(), CPU_REVISION.size()) == 0;
                const bool foundCPUVariant = strncmp(temp, CPU_VARIANT.data(), CPU_VARIANT.size()) == 0;
                const bool foundProcessor = strncmp(temp, PROCESSOR.data(), PROCESSOR.size()) == 0;

                if (foundHardware || foundProcessor || foundCPUArchitecture || foundCPUImplementer || foundCPUPart
                    || foundCPURevision || foundCPUVariant) {
                    char * position = strchr(temp, ':');
                    if (position == nullptr || static_cast<unsigned int>(position - temp) + 2 >= strlen(temp)) {
                        LOG_WARNING("Unknown format of /proc/cpuinfo\n"
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

                    if (foundCPUArchitecture) {
                        int architecture;
                        if (!stringToInt(&architecture, position, 0)) {
                            // Do nothing
                        }
                        else {
                            LOG_DEBUG("   architecture = 0x%x", architecture);
                            if (processor != UNKNOWN_PROCESSOR) {

                                midrs[processor].set_architecture(architecture);
                            }
                            else {
                                outOfPlaceCpuId.set_architecture(architecture);
                                invalidFormat = true;
                            }
                        }
                    }

                    if (foundCPUImplementer) {
                        int implementer;
                        if (!stringToInt(&implementer, position, 0)) {
                            // Do nothing
                        }
                        else {
                            LOG_DEBUG("   implementer = 0x%x", implementer);
                            if (processor != UNKNOWN_PROCESSOR) {
                                midrs[processor].set_implementer(implementer);
                            }
                            else {
                                outOfPlaceCpuId.set_implementer(implementer);
                                invalidFormat = true;
                            }
                        }
                    }

                    if (foundCPUPart) {
                        int part_num;
                        if (!stringToInt(&part_num, position, 0)) {
                            // Do nothing
                        }
                        else {
                            LOG_DEBUG("   part_num = 0x%x", part_num);
                            if (processor != UNKNOWN_PROCESSOR) {
                                midrs[processor].set_partnum(part_num);
                            }
                            else {
                                outOfPlaceCpuId.set_partnum(part_num);
                                invalidFormat = true;
                            }
                        }
                    }

                    if (foundCPURevision) {
                        int revision;
                        if (!stringToInt(&revision, position, 0)) {
                            // Do nothing
                        }
                        else {
                            LOG_DEBUG("   revision = 0x%x", revision);
                            if (processor != UNKNOWN_PROCESSOR) {
                                midrs[processor].set_revision(revision);
                            }
                            else {
                                outOfPlaceCpuId.set_revision(revision);
                                invalidFormat = true;
                            }
                        }
                    }

                    if (foundCPUVariant) {
                        int variant;
                        if (!stringToInt(&variant, position, 0)) {
                            // Do nothing
                        }
                        else {
                            LOG_DEBUG("   variant = 0x%x", variant);
                            if (processor != UNKNOWN_PROCESSOR) {
                                midrs[processor].set_variant(variant);
                            }
                            else {
                                outOfPlaceCpuId.set_variant(variant);
                                invalidFormat = true;
                            }
                        }
                    }

                    if (foundProcessor) {
                        int processorId = -1;
                        const bool converted = stringToInt(&processorId, position, 0);

                        // update min and max processor ids
                        if (converted) {
                            minProcessor =
                                (static_cast<size_t>(processorId) < minProcessor ? processorId : minProcessor);
                            maxProcessor =
                                (static_cast<size_t>(processorId) > maxProcessor ? processorId : maxProcessor);
                        }

                        if (foundProcessorInSection) {
                            // Found a second processor in this section, ignore them all
                            processor = UNKNOWN_PROCESSOR;
                            invalidFormat = true;
                        }
                        else if (converted) {
                            LOG_DEBUG("   processorId = %d", processorId);

                            processor = processorId;
                            if (processor >= midrs.size()) {
                                LOG_ERROR("Found processor %zu but max is %zu", processor, midrs.size());
                                handleException();
                            }
                            foundProcessorInSection = true;
                        }
                    }
                }
            }
            if (fclose(f) == EOF) {
                LOG_WARNING("Failed to close /proc/cpuinfo");
            };

            if (invalidFormat && (outOfPlaceCpuId.valid()) && (minProcessor <= maxProcessor)) {
                minProcessor = (minProcessor > 0 ? minProcessor : 0);
                maxProcessor = (maxProcessor < midrs.size() ? maxProcessor + 1 : midrs.size());

                for (size_t processor = minProcessor; processor < maxProcessor; ++processor) {
                    if (!midrs[processor].valid()) {
                        LOG_DEBUG("Setting global MIDR 0x%08x for processors %zu ",
                                  outOfPlaceCpuId.to_raw_value(),
                                  processor);
                        midrs[processor] = outOfPlaceCpuId;
                    }
                }
            }

            if (!foundCoreName) {
                LOG_FINE("Could not determine core name from /proc/cpuinfo\n"
                         "The core name in the captured xml file will be 'unknown'.");
            }

            return hardwareName;
        }
    }

    std::string readCpuInfo(bool ignoreOffline, bool wantsHardwareName, lib::Span<midr_t> midrs)
    {
        std::map<unsigned, unsigned> cpuToCluster;
        std::map<unsigned, std::set<midr_t>> clusterToMidrs;
        std::map<unsigned, midr_t> cpuToMidrs;

        // first collect the detailed state using the identifier if available
        {
            std::mutex mutex;
            std::condition_variable cv;
            std::size_t identificationThreadCallbackCounter = 0;
            std::map<unsigned, PerCoreIdentificationThread::properties_t> collected_properties {};
            std::vector<std::unique_ptr<PerCoreIdentificationThread>> perCoreThreads {};

            // wake all cores; this ensures the contents of /proc/cpuinfo reflect the full range of cores in the system.
            // this works as follows:
            // - spawn one thread per core that is affined to each core
            // - once all cores are online and affined, *and* have read the data they are required to read, then they callback here to notify this method to continue
            // - the threads remain online until this function finishes (they are disposed of / terminated by destructor); this is so as
            //   to ensure that the cores remain online until cpuinfo is read
            if (!ignoreOffline) {
                for (unsigned cpu = 0; cpu < midrs.size(); ++cpu) {
                    perCoreThreads.emplace_back(new PerCoreIdentificationThread(
                        false,
                        cpu,
                        [&](unsigned c, PerCoreIdentificationThread::properties_t && properties) -> void {
                            std::lock_guard<std::mutex> const guard {mutex};

                            // store it for later processing
                            collected_properties.emplace(c, std::move(properties));

                            // update completed count
                            identificationThreadCallbackCounter += 1;
                            cv.notify_one();
                        }));
                }

                // wait until all threads are online
                std::unique_lock<std::mutex> lock {mutex};
                auto succeeded = cv.wait_for(lock, std::chrono::seconds(10), [&] {
                    return identificationThreadCallbackCounter >= midrs.size();
                });
                if (!succeeded) {
                    LOG_WARNING("Could not identify all CPU cores within the timeout period. Activated %zu of %zu",
                                identificationThreadCallbackCounter,
                                midrs.size());
                }
            }
            //
            // when we don't care about onlining the cores, just read them directly, one by one, any that are offline will be ignored anyway
            //
            else {
                for (unsigned cpu = 0; cpu < midrs.size(); ++cpu) {
                    if (collected_properties.count(cpu) == 0) {
                        auto const properties = PerCoreIdentificationThread::detectFor(cpu);
                        collected_properties.emplace(cpu, properties);
                    }
                }
            }

            // lock to prevent concurrent access to maps if one of the threads stalls
            std::lock_guard<std::mutex> const lock(mutex);

            // process the collected properties
            for (auto const & entry : collected_properties) {
                auto c = entry.first;
                auto const & properties = entry.second;

                // store the cluster / core mappings to allow us to fill in any gaps by assuming the same core type per cluster
                if (properties.physical_package_id != PerCoreIdentificationThread::INVALID_PACKAGE_ID) {
                    cpuToCluster[c] = properties.physical_package_id;

                    // also map cluster to MIDR value if read
                    if (properties.midr_el1 != PerCoreIdentificationThread::INVALID_MIDR_EL1) {
                        clusterToMidrs[properties.physical_package_id].insert(midr_t::from_raw(properties.midr_el1));
                    }

                    for (int sibling : properties.core_siblings) {
                        const unsigned sibling_cpu = sibling;

                        if (cpuToCluster.count(sibling_cpu) == 0) {
                            cpuToCluster[sibling_cpu] = properties.physical_package_id;
                        }
                    }
                }

                // map cpu to MIDR value if read
                if (properties.midr_el1 != PerCoreIdentificationThread::INVALID_MIDR_EL1) {
                    cpuToMidrs[c] = midr_t::from_raw(properties.midr_el1);
                }
            }
        }

        // log what we learnt
        for (const auto & pair : cpuToMidrs) {
            LOG_FINE("Read CPU %u MIDR_EL1 -> 0x%08x", pair.first, pair.second.to_raw_value());
        }
        for (const auto & pair : cpuToCluster) {
            LOG_FINE("Read CPU %u CLUSTER %u", pair.first, pair.second);
        }
        for (const auto & pair : clusterToMidrs) {
            LOG_FINE("Read CLUSTER %u MIDRs:", pair.first);
            for (auto const & midr : pair.second) {
                LOG_FINE("    0x%08x", midr.to_raw_value());
            }
        }

        // did we successfully read all MIDR values from all cores?
        const bool knowAllMidrValues = (cpuToMidrs.size() == midrs.size());

        // do we need to read /proc/cpuinfo
        std::string hardwareName = (wantsHardwareName || (!knowAllMidrValues && !ignoreOffline)
                                        ? parseProcCpuInfo(/* justGetHardwareName = */ knowAllMidrValues, midrs)
                                        : "");

        // update/set known items from MIDR map and topology information. This will override anything read from /proc/cpuinfo
        updateCpuIdsFromTopologyInformation(midrs, cpuToMidrs, cpuToCluster, clusterToMidrs);

        return hardwareName;
    }
}
