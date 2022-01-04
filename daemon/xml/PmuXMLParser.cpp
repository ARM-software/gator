/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

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

#include <sys/types.h>
#include <unistd.h>

static const char TAG_PMUS[] = "pmus";
static const char TAG_PMU[] = "pmu";
static const char TAG_UNCORE_PMU[] = "uncore_pmu";
static const char TAG_CPUID[] = "cpuid";

static const char ATTR_ID[] = "id";
static const char ATTR_COUNTER_SET[] = "counter_set";
static const char ATTR_CPUID[] = "cpuid";
static const char ATTR_CORE_NAME[] = "core_name";
static const char ATTR_DT_NAME[] = "dt_name";
static const char ATTR_SPE_NAME[] = "spe";
static const char ATTR_PMNC_COUNTERS[] = "pmnc_counters";
static const char ATTR_PROFILE[] = "profile";
static const char ATTR_HAS_CYCLES_COUNTER[] = "has_cycles_counter";
static const char ATTR_DEVICE_INSTANCE[] = "device_instance";
static const char UNCORE_PMNC_NAME_WILDCARD_D[] = "%d";
static const char UNCORE_PMNC_NAME_WILDCARD_S[] = "%s";

#define PERF_DEVICES "/sys/bus/event_source/devices"

static bool matchPMUName(const char * const pmu_name, const char * const test_name, size_t & wc_from, size_t & wc_len)
{
    const char * const percentD = strstr(pmu_name, UNCORE_PMNC_NAME_WILDCARD_D);

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
        if (strcasecmp(pmu_name + offset + sizeof(UNCORE_PMNC_NAME_WILDCARD_D) - 1, test_name + test_offset) != 0) {
            return false;
        }

        // store the start and length of the matched pattern
        wc_from = offset;
        wc_len = test_offset - offset;
        return true;
    }

    const char * const percentS = strstr(pmu_name, UNCORE_PMNC_NAME_WILDCARD_S);

    // did we match the string suffix marker?
    if (percentS != nullptr) {
        // match prefix up to but not including wildcard
        const size_t offset = percentS - pmu_name;
        if (strncasecmp(pmu_name, test_name, offset) != 0) {
            return false;
        }

        // the wildcard must be at the end of the pmu_name
        if (strcasecmp(percentS, UNCORE_PMNC_NAME_WILDCARD_S) != 0) {
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

static bool parseCpuId(std::set<int> & cpuIds,
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

    if ((cpuid != 0) && ((cpuid & 0xfffff) != 0xfffff)) {
        cpuIds.insert(cpuid);
        return true;
    }
    LOG_ERROR("The %s for '%s' in pmu XML is not valid", locationStr, pmuId);
    return false;
}

bool parseXml(const char * const xml, PmuXML & pmuXml)
{
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> documentPtr {mxmlLoadString(nullptr, xml, MXML_NO_CALLBACK),
                                                                       mxmlDelete};

    // find the root
    mxml_node_t * const root =
        ((documentPtr == nullptr) || (strcmp(mxmlGetElement(documentPtr.get()), TAG_PMUS) == 0)
             ? documentPtr.get()
             : mxmlFindElement(documentPtr.get(), documentPtr.get(), TAG_PMUS, nullptr, nullptr, MXML_DESCEND));
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

    for (mxml_node_t * node = mxmlFindElement(root, root, TAG_PMU, nullptr, nullptr, MXML_DESCEND); node != nullptr;
         node = mxmlFindElement(node, root, TAG_PMU, nullptr, nullptr, MXML_DESCEND)) {
        // attributes
        const char * const id = mxmlElementGetAttr(node, ATTR_ID);
        const char * const counterSetAttr = mxmlElementGetAttr(node, ATTR_COUNTER_SET);
        const char * const counterSet = (counterSetAttr != nullptr ? counterSetAttr : id); // uses id as default
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME);
        const char * const dtName = mxmlElementGetAttr(node, ATTR_DT_NAME);
        const char * const speName = mxmlElementGetAttr(node, ATTR_SPE_NAME);
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS);
        const char * const profileStr = mxmlElementGetAttr(node, ATTR_PROFILE);

        // read cpuid(s)
        std::set<int> cpuIds;
        {
            if (!parseCpuId(cpuIds, mxmlElementGetAttr(node, ATTR_CPUID), false, id, "cpuid attribute")) {
                return false;
            }
            for (mxml_node_t * childNode = mxmlFindElement(node, node, TAG_CPUID, nullptr, nullptr, MXML_DESCEND);
                 childNode != nullptr;
                 childNode = mxmlFindElement(childNode, node, TAG_CPUID, nullptr, nullptr, MXML_DESCEND)) {
                if (!parseCpuId(cpuIds, mxmlElementGetAttr(childNode, ATTR_ID), true, id, "cpuid.id attribute")) {
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
                      ATTR_ID,
                      ATTR_CPUID,
                      ATTR_CORE_NAME,
                      ATTR_PMNC_COUNTERS);
            return false;
        }

        //Check whether the pmu is v8, needed when hardware is 64 bit but kernel is 32 bit.
        bool isV8 = false;
        if (profileStr != nullptr) {
            if (profileStr[0] == '8') {
                isV8 = true;
            }
        }

        LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"0x%05x\" %s=\"%d\" />",
                  TAG_PMU,
                  ATTR_CORE_NAME,
                  coreName,
                  ATTR_ID,
                  id,
                  ATTR_COUNTER_SET,
                  counterSet,
                  ATTR_CPUID,
                  *cpuIds.begin(),
                  ATTR_PMNC_COUNTERS,
                  pmncCounters);

        pmuXml.cpus.emplace_back(coreName, id, counterSet, dtName, speName, std::move(cpuIds), pmncCounters, isV8);
    }

    for (mxml_node_t * node = mxmlFindElement(root, root, TAG_UNCORE_PMU, nullptr, nullptr, MXML_DESCEND);
         node != nullptr;
         node = mxmlFindElement(node, root, TAG_UNCORE_PMU, nullptr, nullptr, MXML_DESCEND)) {
        const char * const id = mxmlElementGetAttr(node, ATTR_ID);
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME);
        const char * const counterSetAttr = mxmlElementGetAttr(node, ATTR_COUNTER_SET);
        const char * const counterSet =
            (counterSetAttr != nullptr ? counterSetAttr : coreName); // uses core name as default
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS);
        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            LOG_ERROR("The pmnc_counters for '%s' in pmu XML is not an integer", id);
            return false;
        }
        const char * const hasCyclesCounterStr = mxmlElementGetAttr(node, ATTR_HAS_CYCLES_COUNTER);
        const bool hasCyclesCounter = stringToBool(hasCyclesCounterStr, true);
        if ((id == nullptr) || (strlen(id) == 0) || (counterSet == nullptr) || (strlen(counterSet) == 0)
            || (coreName == nullptr) || (strlen(coreName) == 0) || (pmncCounters == 0)) {
            LOG_ERROR(
                "An uncore_pmu from the pmu XML is missing one or more of the required attributes (%s, %s and %s)",
                ATTR_ID,
                ATTR_CORE_NAME,
                ATTR_PMNC_COUNTERS);
            return false;
        }

        // check if the path contains a wildcard
        if ((strstr(id, UNCORE_PMNC_NAME_WILDCARD_D) == nullptr)
            && (strstr(id, UNCORE_PMNC_NAME_WILDCARD_S) == nullptr)) {
            // no - just add one item
            LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                      TAG_UNCORE_PMU,
                      ATTR_CORE_NAME,
                      coreName,
                      ATTR_ID,
                      id,
                      ATTR_COUNTER_SET,
                      counterSet,
                      ATTR_HAS_CYCLES_COUNTER,
                      hasCyclesCounter ? "true" : "false",
                      ATTR_PMNC_COUNTERS,
                      pmncCounters);

            pmuXml.uncores.emplace_back(coreName, id, counterSet, "", pmncCounters, hasCyclesCounter);
        }
        else {
            // yes - add actual matching items from filesystem
            bool matched = false;
            lib::FsEntryDirectoryIterator it = lib::FsEntry::create(PERF_DEVICES).children();
            std::optional<lib::FsEntry> child;
            while (!!(child = it.next())) {
                size_t wc_start = 0;
                size_t wc_len = 0;
                if (matchPMUName(id, child->name().c_str(), wc_start, wc_len)) {
                    std::string patternPart = child->name().substr(wc_start, wc_len);

                    // matched dirent
                    LOG_DEBUG("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" %s=\"%s\" />",
                              TAG_UNCORE_PMU,
                              ATTR_CORE_NAME,
                              coreName,
                              ATTR_ID,
                              child->name().c_str(),
                              ATTR_COUNTER_SET,
                              counterSet,
                              ATTR_HAS_CYCLES_COUNTER,
                              hasCyclesCounter ? "true" : "false",
                              ATTR_PMNC_COUNTERS,
                              pmncCounters,
                              ATTR_DEVICE_INSTANCE,
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
                          TAG_UNCORE_PMU,
                          ATTR_CORE_NAME,
                          coreName,
                          ATTR_ID,
                          id,
                          ATTR_COUNTER_SET,
                          counterSet,
                          ATTR_HAS_CYCLES_COUNTER,
                          hasCyclesCounter ? "true" : "false",
                          ATTR_PMNC_COUNTERS,
                          pmncCounters);
            }
        }
    }

    return true;
}

PmuXML readPmuXml(const char * const path)
{
    PmuXML pmuXml {};

    {
        if (!parseXml(PmuXML::DEFAULT_XML, pmuXml)) {
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
