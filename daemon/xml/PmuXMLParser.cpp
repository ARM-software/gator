/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "xml/PmuXMLParser.h"

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/String.h"
#include "lib/Utils.h"
#include "mxml/mxml.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include <boost/regex.hpp>

#include <sys/types.h>
#include <unistd.h>

using namespace std::literals;

namespace {
    constexpr auto TAG_PMUS = "pmus"sv;
    constexpr auto TAG_PMU = "pmu"sv;
    constexpr auto TAG_UNCORE_PMU = "uncore_pmu"sv;
    constexpr auto TAG_CPUID = "cpuid"sv;
    constexpr auto TAG_SMMUV3 = "smmuv3"sv;

    constexpr auto ATTR_ID = "id"sv;
    constexpr auto ATTR_COUNTER_SET = "counter_set"sv;
    constexpr auto ATTR_CPUID = "cpuid"sv;
    constexpr auto ATTR_CORE_NAME = "core_name"sv;
    constexpr auto ATTR_DT_NAME = "dt_name"sv;
    constexpr auto ATTR_SPE_NAME = "spe"sv;
    constexpr auto ATTR_PMNC_COUNTERS = "pmnc_counters"sv;
    constexpr auto ATTR_PROFILE = "profile"sv;
    constexpr auto ATTR_HAS_CYCLES_COUNTER = "has_cycles_counter"sv;
    constexpr auto ATTR_DEVICE_INSTANCE = "device_instance"sv;
    constexpr auto UNCORE_PMNC_NAME_WILDCARD_D = "%d"sv;
    constexpr auto UNCORE_PMNC_NAME_WILDCARD_S = "%s"sv;

    constexpr auto SMMUV3_TBU_TOKEN = "TBU"sv;
    constexpr auto SMMUV3_TCU_TOKEN = "TCU"sv;
    constexpr auto SMMUV3_DEFAULT_TBU_COUNTER_SET = "SMMUv3_TBU"sv;
    constexpr auto SMMUV3_DEFAULT_TCU_COUNTER_SET = "SMMUv3_TCU"sv;

    constexpr unsigned int cpuid_mask = 0xfffff;

    constexpr auto perf_devices = "/sys/bus/event_source/devices"sv;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    bool matchPMUName(const char * const pmu_name, const char * const test_name, size_t & wc_from, size_t & wc_len)
    {
        const char * const percentD = strstr(pmu_name, UNCORE_PMNC_NAME_WILDCARD_D.data());

        // did we match the numeric marker?
        if (percentD != nullptr) {
            // match prefix up to but not including wildcard
            const size_t offset = percentD - pmu_name;
            if (strncasecmp(pmu_name, test_name, offset) != 0) {
                return false;
            }

            // find first character after wildcard in test_name
            size_t test_offset;
            for (test_offset = offset; test_name[test_offset] != 0; ++test_offset) {
                const char c = test_name[test_offset];
                if ((c < '0') || (c > '9')) {
                    break;
                }
            }

            // compare suffix
            if (strcasecmp(pmu_name + offset + UNCORE_PMNC_NAME_WILDCARD_D.size(), test_name + test_offset) != 0) {
                return false;
            }

            // store the start and length of the matched pattern
            wc_from = offset;
            wc_len = test_offset - offset;
            return true;
        }

        const char * const percentS = strstr(pmu_name, UNCORE_PMNC_NAME_WILDCARD_S.data());

        // did we match the string suffix marker?
        if (percentS != nullptr) {
            // match prefix up to but not including wildcard
            const size_t offset = percentS - pmu_name;
            if (strncasecmp(pmu_name, test_name, offset) != 0) {
                return false;
            }

            // the wildcard must be at the end of the pmu_name
            if (strcasecmp(percentS, UNCORE_PMNC_NAME_WILDCARD_S.data()) != 0) {
                return false;
            }

            // store the start and length of the matched pattern
            wc_from = offset;
            wc_len = strlen(test_name) - offset;
            return true;
        }

        // ok, no pattern matched
        wc_from = 0;
        wc_len = 0;
        return strcasecmp(pmu_name, test_name) == 0;
    }

    bool parseCpuId(std::set<int> & cpuIds,
                    const char * const cpuIdStr,
                    bool required,
                    const char * const pmuId,
                    const char * const locationStr)
    {
        if (cpuIdStr == nullptr) {
            if (required) {
                LOG_ERROR("The %s for '%s' in pmus.xml is missing", locationStr, pmuId);
                return false;
            }
            return true;
        }

        int cpuid;
        if (!stringToInt(&cpuid, cpuIdStr, 0)) {
            LOG_ERROR("The %s for '%s' in pmu XML is not an integer", locationStr, pmuId);
            return false;
        }

        if ((cpuid != 0) && ((static_cast<unsigned int>(cpuid) & cpuid_mask) != cpuid_mask)) {
            cpuIds.insert(cpuid);
            return true;
        }
        LOG_ERROR("The %s for '%s' in pmu XML is not valid", locationStr, pmuId);
        return false;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::string_view work_out_smmuv3_counter_set(std::string_view id_attr, const char * counter_set_attr)
    {
        if (counter_set_attr != nullptr) {
            return {counter_set_attr};
        }

        if (id_attr.find(SMMUV3_TBU_TOKEN) != std::string::npos) {
            return SMMUV3_DEFAULT_TBU_COUNTER_SET;
        }
        if (id_attr.find(SMMUV3_TCU_TOKEN) != std::string::npos) {
            return SMMUV3_DEFAULT_TCU_COUNTER_SET;
        }
        return {};
    }

    bool parse_smmuv3(PmuXML & pmu_xml, mxml_node_t * node)
    {
        const auto * id_attr = mxmlElementGetAttr(node, ATTR_ID.data());
        const auto * core_name_attr = mxmlElementGetAttr(node, ATTR_CORE_NAME.data());
        const auto * counter_set_attr = mxmlElementGetAttr(node, ATTR_COUNTER_SET.data());
        const auto * pmnc_counters_attr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS.data());

        if (id_attr == nullptr || strlen(id_attr) == 0) {
            LOG_ERROR("An smmuv3 element is missing the required [id] attribute");
            return false;
        }

        if (core_name_attr == nullptr || strlen(core_name_attr) == 0) {
            LOG_ERROR("The smmuv3 element with id [%s] is missing the required [core_name] attribute", id_attr);
            return false;
        }

        int pmnc_counters;
        if (!stringToInt(&pmnc_counters, pmnc_counters_attr, 0)) {
            LOG_ERROR("The pmnc_counters for '%s' in pmu XML is not an integer", id_attr);
            return false;
        }

        if (pmnc_counters == 0) {
            LOG_ERROR("The smmuv3 element with ID [%s] is missing the required [pmnc_counters] attribute", id_attr);
            return false;
        }

        const auto counter_set = work_out_smmuv3_counter_set(id_attr, counter_set_attr);
        if (counter_set.empty()) {
            LOG_ERROR("The smmuv3 element with ID [%s] does not have a [counter_set] attribute and the counter set"
                      " could not be determined from the ID. Please ensure the ID contains either \"TBU\" or "
                      "\"TCU\", or include an explicit [counter_set] attribute.",
                      id_attr);
            return false;
        }

        std::optional<gator::smmuv3::iidr_t> iidr {};
        // try and parse an IIDR value from the PMU ID
        boost::regex pattern {".*([0-9A-Fa-f]{3})([0-9A-Fa-f]{2}|_)([0-9A-Fa-f]{3}).*"};
        boost::cmatch match;
        if (boost::regex_match(id_attr, match, pattern)) {
            iidr.emplace(std::array<std::string, 3> {match[1].str(), match[2].str(), match[3].str()});
        }

        pmu_xml.smmu_pmus.emplace_back(core_name_attr,
                                       id_attr,
                                       std::string(counter_set),
                                       pmnc_counters,
                                       std::move(iidr));
        return true;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool parseXml(const char * const xml, PmuXML & pmuXml)
{
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> documentPtr {mxmlLoadString(nullptr, xml, MXML_NO_CALLBACK),
                                                                       mxmlDelete};

    // find the root
    mxml_node_t * const root =
        ((documentPtr == nullptr) || (strcmp(mxmlGetElement(documentPtr.get()), TAG_PMUS.data()) == 0)
             ? documentPtr.get()
             : mxmlFindElement(documentPtr.get(), documentPtr.get(), TAG_PMUS.data(), nullptr, nullptr, MXML_DESCEND));
    if (root == nullptr) {
        LOG_ERROR("Invalid 'pmus.xml'");
        return false;
    }

    const char * const versionStr = mxmlElementGetAttr(root, "version");
    if ((versionStr == nullptr) || (strcmp(versionStr, "2") != 0)) {
        LOG_ERROR("Invalid or missing version string in 'pmus.xml': (%s)",
                  (versionStr != nullptr ? versionStr : "<missing>"));
        return false;
    }

    for (mxml_node_t * node = mxmlFindElement(root, root, TAG_PMU.data(), nullptr, nullptr, MXML_DESCEND);
         node != nullptr;
         node = mxmlFindElement(node, root, TAG_PMU.data(), nullptr, nullptr, MXML_DESCEND)) {
        // attributes
        const char * const id = mxmlElementGetAttr(node, ATTR_ID.data());
        const char * const counterSetAttr = mxmlElementGetAttr(node, ATTR_COUNTER_SET.data());
        const char * const counterSet = (counterSetAttr != nullptr ? counterSetAttr : id); // uses id as default
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME.data());
        const char * const dtName = mxmlElementGetAttr(node, ATTR_DT_NAME.data());
        const char * speName = mxmlElementGetAttr(node, ATTR_SPE_NAME.data());
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS.data());
        const char * const profileStr = mxmlElementGetAttr(node, ATTR_PROFILE.data());

        // read cpuid(s)
        std::set<int> cpuIds;
        {
            if (!parseCpuId(cpuIds, mxmlElementGetAttr(node, ATTR_CPUID.data()), false, id, "cpuid attribute")) {
                return false;
            }
            for (mxml_node_t * childNode =
                     mxmlFindElement(node, node, TAG_CPUID.data(), nullptr, nullptr, MXML_DESCEND);
                 childNode != nullptr;
                 // NOLINTNEXTLINE(readability-suspicious-call-argument)
                 childNode = mxmlFindElement(childNode, node, TAG_CPUID.data(), nullptr, nullptr, MXML_DESCEND)) {
                if (!parseCpuId(cpuIds,
                                mxmlElementGetAttr(childNode, ATTR_ID.data()),
                                true,
                                id,
                                "cpuid.id attribute")) {
                    return false;
                }
            }
        }

        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            LOG_ERROR("The pmnc_counters for '%s' in pmu XML is not an integer", id);
            return false;
        }
        if ((id == nullptr) || (strlen(id) == 0) || (counterSet == nullptr) || (strlen(counterSet) == 0)
            || cpuIds.empty() || (coreName == nullptr) || (strlen(coreName) == 0) || (pmncCounters <= 0)) {
            LOG_ERROR("A pmu from the pmu XML is missing one or more of the required attributes (%s, %s, %s and %s)",
                      ATTR_ID.data(),
                      ATTR_CPUID.data(),
                      ATTR_CORE_NAME.data(),
                      ATTR_PMNC_COUNTERS.data());
            return false;
        }

        //Check whether the pmu is v8, needed when hardware is 64 bit but kernel is 32 bit.
        bool isV8 = false;
        if (profileStr != nullptr) {
            if ((profileStr[0] == '8') || (profileStr[0] == '9')) {
                isV8 = true;
            }
        }

        LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"0x%05x\" %s=\"%d\" />",
                  TAG_PMU.data(),
                  ATTR_CORE_NAME.data(),
                  coreName,
                  ATTR_ID.data(),
                  id,
                  ATTR_COUNTER_SET.data(),
                  counterSet,
                  ATTR_CPUID.data(),
                  *cpuIds.begin(),
                  ATTR_PMNC_COUNTERS.data(),
                  pmncCounters);

        // Check if SPE name is specified for the given CPU. If so, check to see if the SPE device is configured on the device.
        if (speName != nullptr)
        {
            bool speDeviceFound = false;
            lib::FsEntryDirectoryIterator it = lib::FsEntry::create(perf_devices.data()).children();
            std::optional<lib::FsEntry> child;
            while (!!(child = it.next())) {
                if (child->name().find("spe") != std::string::npos) {
                    // SPE device found in /sys/bus/event_source/devices
                    speDeviceFound = true;
                    break;
                }
            }

            if (!speDeviceFound)
            {
                speName = nullptr;
            }
        }

        pmuXml.cpus.emplace_back(coreName, id, counterSet, dtName, speName, std::move(cpuIds), pmncCounters, isV8);
    }

    for (mxml_node_t * node = mxmlFindElement(root, root, TAG_UNCORE_PMU.data(), nullptr, nullptr, MXML_DESCEND);
         node != nullptr;
         node = mxmlFindElement(node, root, TAG_UNCORE_PMU.data(), nullptr, nullptr, MXML_DESCEND)) {
        const char * const id = mxmlElementGetAttr(node, ATTR_ID.data());
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME.data());
        const char * const counterSetAttr = mxmlElementGetAttr(node, ATTR_COUNTER_SET.data());
        const char * const counterSet =
            (counterSetAttr != nullptr ? counterSetAttr : coreName); // uses core name as default
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS.data());
        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            LOG_ERROR("The pmnc_counters for '%s' in pmu XML is not an integer", id);
            return false;
        }
        const char * const hasCyclesCounterStr = mxmlElementGetAttr(node, ATTR_HAS_CYCLES_COUNTER.data());
        const bool hasCyclesCounter = stringToBool(hasCyclesCounterStr, true);
        if ((id == nullptr) || (strlen(id) == 0) || (counterSet == nullptr) || (strlen(counterSet) == 0)
            || (coreName == nullptr) || (strlen(coreName) == 0) || (pmncCounters == 0)) {
            LOG_ERROR(
                "An uncore_pmu from the pmu XML is missing one or more of the required attributes (%s, %s and %s)",
                ATTR_ID.data(),
                ATTR_CORE_NAME.data(),
                ATTR_PMNC_COUNTERS.data());
            return false;
        }

        // check if the path contains a wildcard
        if ((strstr(id, UNCORE_PMNC_NAME_WILDCARD_D.data()) == nullptr)
            && (strstr(id, UNCORE_PMNC_NAME_WILDCARD_S.data()) == nullptr)) {
            // no - just add one item
            LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                      TAG_UNCORE_PMU.data(),
                      ATTR_CORE_NAME.data(),
                      coreName,
                      ATTR_ID.data(),
                      id,
                      ATTR_COUNTER_SET.data(),
                      counterSet,
                      ATTR_HAS_CYCLES_COUNTER.data(),
                      hasCyclesCounter ? "true" : "false",
                      ATTR_PMNC_COUNTERS.data(),
                      pmncCounters);

            pmuXml.uncores.emplace_back(coreName, id, counterSet, "", pmncCounters, hasCyclesCounter);
        }
        else {
            // yes - add actual matching items from filesystem
            bool matched = false;
            lib::FsEntryDirectoryIterator it = lib::FsEntry::create(perf_devices.data()).children();
            std::optional<lib::FsEntry> child;
            while (!!(child = it.next())) {
                size_t wc_start = 0;
                size_t wc_len = 0;
                if (matchPMUName(id, child->name().c_str(), wc_start, wc_len)) {
                    std::string patternPart = child->name().substr(wc_start, wc_len);

                    // matched dirent
                    LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" %s=\"%s\" />",
                              TAG_UNCORE_PMU.data(),
                              ATTR_CORE_NAME.data(),
                              coreName,
                              ATTR_ID.data(),
                              child->name().c_str(),
                              ATTR_COUNTER_SET.data(),
                              counterSet,
                              ATTR_HAS_CYCLES_COUNTER.data(),
                              hasCyclesCounter ? "true" : "false",
                              ATTR_PMNC_COUNTERS.data(),
                              pmncCounters,
                              ATTR_DEVICE_INSTANCE.data(),
                              patternPart.c_str());
                    pmuXml.uncores.emplace_back(coreName,
                                                child->name(),
                                                counterSet,
                                                std::move(patternPart),
                                                pmncCounters,
                                                hasCyclesCounter);
                    matched = true;
                }
                else {
                    LOG_DEBUG("no match '%s' for '%s'", child->name().c_str(), id);
                }
            }

            if (!matched) {
                LOG_DEBUG("No matching devices for wildcard <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                          TAG_UNCORE_PMU.data(),
                          ATTR_CORE_NAME.data(),
                          coreName,
                          ATTR_ID.data(),
                          id,
                          ATTR_COUNTER_SET.data(),
                          counterSet,
                          ATTR_HAS_CYCLES_COUNTER.data(),
                          hasCyclesCounter ? "true" : "false",
                          ATTR_PMNC_COUNTERS.data(),
                          pmncCounters);
            }
        }
    }

    for (mxml_node_t * node = mxmlFindElement(root, root, TAG_SMMUV3.data(), nullptr, nullptr, MXML_DESCEND);
         node != nullptr;
         node = mxmlFindElement(node, root, TAG_SMMUV3.data(), nullptr, nullptr, MXML_DESCEND)) {
        if (!parse_smmuv3(pmuXml, node)) {
            return false;
        }
    }

    return true;
}

PmuXML readPmuXml(const char * const path)
{
    PmuXML pmuXml {};

    {
        if (!parseXml(PmuXML::DEFAULT_XML.data(), pmuXml)) {
            handleException();
        }
    }

    if (path != nullptr) {
        // Parse user defined items second as they will show up first in the linked list
        std::unique_ptr<char, void (*)(void *)> xml {readFromDisk(path), std::free};
        if (xml == nullptr) {
            LOG_ERROR("Unable to open additional pmus XML %s", path);
            handleException();
        }
        if (!parseXml(xml.get(), pmuXml)) {
            handleException();
        }
    }

    return pmuXml;
}
