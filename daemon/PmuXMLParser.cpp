/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PmuXMLParser.h"

#include <algorithm>
#include <cstring>

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include "mxml/mxml.h"

#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/String.h"
#include "lib/Utils.h"

static const char TAG_PMU[] = "pmu";
static const char TAG_UNCORE_PMU[] = "uncore_pmu";

static const char ATTR_PMNC_NAME[] = "pmnc_name";
static const char ATTR_CPUID[] = "cpuid";
static const char ATTR_CORE_NAME[] = "core_name";
static const char ATTR_DT_NAME[] = "dt_name";
static const char ATTR_SPE_NAME[] = "spe";
static const char ATTR_PMNC_COUNTERS[] = "pmnc_counters";
static const char AATR_PROFILE[] = "profile";
static const char ATTR_HAS_CYCLES_COUNTER[] = "has_cycles_counter";
static const char UNCORE_PMNC_NAME_WILDCARD[] = "%d";

#define PERF_DEVICES "/sys/bus/event_source/devices"


static void getDefaultXml(const char ** const xml, unsigned int * const len)
{
#include "pmus_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len
    *xml = reinterpret_cast<const char *>(pmus_xml);
    *len = pmus_xml_len;
}


static bool matchPMUName(const char * const pmu_name, const char * const test_name)
{
    const char * const percent = strstr(pmu_name, UNCORE_PMNC_NAME_WILDCARD);

    if (percent == NULL) {
        return strcasecmp(pmu_name, test_name) == 0;
    }

    // match prefix up to but not including wildcard
    const int offset = (percent != NULL ? percent - pmu_name : 0);
    if (strncasecmp(pmu_name, test_name, offset) != 0) {
        return false;
    }

    // find first character after wildcard in test_name
    int test_offset;
    for (test_offset = offset; test_name[test_offset] != 0; ++test_offset) {
        const char c = test_name[test_offset];
        if ((c < '0') || (c > '9')) {
            break;
        }
    }

    // compare suffix
    return strcasecmp(pmu_name + offset + sizeof(UNCORE_PMNC_NAME_WILDCARD) - 1, test_name + test_offset) == 0;
}

static void parse(PmuXML & pmuXml, const char * const xml)
{
    mxml_node_t *root = mxmlLoadString(NULL, xml, MXML_NO_CALLBACK);

    for (mxml_node_t *node = mxmlFindElement(root, root, TAG_PMU, NULL, NULL, MXML_DESCEND); node != NULL;
            node = mxmlFindElement(node, root, TAG_PMU, NULL, NULL, MXML_DESCEND)) {
        const char * const pmncName = mxmlElementGetAttr(node, ATTR_PMNC_NAME);
        const char * const cpuidStr = mxmlElementGetAttr(node, ATTR_CPUID);
        int cpuid;
        if (!stringToInt(&cpuid, cpuidStr, 0)) {
            logg.logError("The cpuid for '%s' in pmu XML is not an integer", pmncName);
            handleException();
        }
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME);
        const char * const dtName = mxmlElementGetAttr(node, ATTR_DT_NAME);
        const char * const speName = mxmlElementGetAttr(node, ATTR_SPE_NAME);
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS);
        const char * const profileStr = mxmlElementGetAttr(node, AATR_PROFILE);
        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            logg.logError("The pmnc_counters for '%s' in pmu XML is not an integer", pmncName);
            handleException();
        }
        if (pmncName == NULL || cpuid == 0 || coreName == NULL || pmncCounters == 0) {
            logg.logError("A pmu from the pmu XML is missing one or more of the required attributes (%s, %s, %s and %s)", ATTR_PMNC_NAME, ATTR_CPUID, ATTR_CORE_NAME, ATTR_PMNC_COUNTERS);
            handleException();
        }

        //Check whether the pmu is v8, needed when hardware is 64 bit but kernel is 32 bit.
        bool isV8 = false;
        if (profileStr != nullptr) {
            if (profileStr[0] == '8') {
                isV8 = true;
            }
        }

        logg.logMessage("Found <%s %s=\"%s\" %s=\"%s\" %s=\"0x%05x\" %s=\"%d\" />",
                        TAG_PMU,
                        ATTR_CORE_NAME, coreName,
                        ATTR_PMNC_NAME, pmncName,
                        ATTR_CPUID, cpuid,
                        ATTR_PMNC_COUNTERS, pmncCounters);

        pmuXml.cpus.emplace_back(strdup(coreName), strdup(pmncName), lib::strdup_null(dtName),
                                 lib::strdup_null(speName), cpuid, pmncCounters, isV8);
    }

    for (mxml_node_t *node = mxmlFindElement(root, root, TAG_UNCORE_PMU, NULL, NULL, MXML_DESCEND); node != NULL;
            node = mxmlFindElement(node, root, TAG_UNCORE_PMU, NULL, NULL, MXML_DESCEND)) {
        const char * const pmncName = mxmlElementGetAttr(node, ATTR_PMNC_NAME);
        const char * const coreName = mxmlElementGetAttr(node, ATTR_CORE_NAME);
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS);
        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            logg.logError("The pmnc_counters for '%s' in pmu XML is not an integer", pmncName);
            handleException();
        }
        const char * const hasCyclesCounterStr = mxmlElementGetAttr(node, ATTR_HAS_CYCLES_COUNTER);
        const bool hasCyclesCounter = stringToBool(hasCyclesCounterStr, true);
        if (pmncName == NULL || coreName == NULL || pmncCounters == 0) {
            logg.logError("An uncore_pmu from the pmu XML is missing one or more of the required attributes (%s, %s and %s)", ATTR_PMNC_NAME, ATTR_CORE_NAME, ATTR_PMNC_COUNTERS);
            handleException();
        }

        // check if the path contains a wildcard
        if (strstr(pmncName, UNCORE_PMNC_NAME_WILDCARD) == nullptr)
        {
            // no - just add one item
            logg.logMessage("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                            TAG_UNCORE_PMU,
                            ATTR_CORE_NAME, coreName,
                            ATTR_PMNC_NAME, pmncName,
                            ATTR_HAS_CYCLES_COUNTER, hasCyclesCounter ? "true" : "false",
                            ATTR_PMNC_COUNTERS, pmncCounters);

            pmuXml.uncores.emplace_back(strdup(coreName), strdup(pmncName), pmncCounters, hasCyclesCounter);
        }
        else
        {
            // yes - add actual matching items from filesystem
            bool matched = false;
            std::unique_ptr<DIR, int (*)(DIR*)> dir { opendir(PERF_DEVICES), closedir };
            if (dir != nullptr) {
                struct dirent * dirent;
                while ((dirent = readdir(dir.get())) != NULL) {
                    if (matchPMUName(pmncName, dirent->d_name)) {
                        // matched dirent
                        logg.logMessage("Found <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                                        TAG_UNCORE_PMU,
                                        ATTR_CORE_NAME, coreName,
                                        ATTR_PMNC_NAME, dirent->d_name,
                                        ATTR_HAS_CYCLES_COUNTER, hasCyclesCounter ? "true" : "false",
                                        ATTR_PMNC_COUNTERS, pmncCounters);
                        new UncorePmu(strdup(coreName), strdup(dirent->d_name), pmncCounters, hasCyclesCounter);
                        matched = true;
                    }
                }
            }

            if (!matched) {
                logg.logMessage("No matching devices for wildcard <%s %s=\"%s\" %s=\"%s\" %s=\"%s\" %s=\"%d\" />",
                                TAG_UNCORE_PMU,
                                ATTR_CORE_NAME, coreName,
                                ATTR_PMNC_NAME, pmncName,
                                ATTR_HAS_CYCLES_COUNTER, hasCyclesCounter ? "true" : "false",
                                ATTR_PMNC_COUNTERS, pmncCounters);
            }
        }
    }

    mxmlDelete(root);
}

PmuXML readPmuXml(const char * const path)
{
    PmuXML pmuXml {{},{}};
    {
        const char *xml;
        unsigned int len;
        getDefaultXml(&xml, &len);
        parse(pmuXml, xml);
    }

    if (path != NULL) {
        // Parse user defined items second as they will show up first in the linked list
        char *xml = readFromDisk(path);
        if (xml == NULL) {
            logg.logError("Unable to open additional pmus XML %s", path);
            handleException();
        }
        parse(pmuXml, xml);
        free(xml);
    }
    return pmuXml;
}



