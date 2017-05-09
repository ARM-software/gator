/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PmuXML.h"

#include <algorithm>

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include "mxml/mxml.h"

#include "DriverSource.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

static const char TAG_PMU[] = "pmu";
static const char TAG_UNCORE_PMU[] = "uncore_pmu";

static const char ATTR_PMNC_NAME[] = "pmnc_name";
static const char ATTR_CPUID[] = "cpuid";
static const char ATTR_CORE_NAME[] = "core_name";
static const char ATTR_DT_NAME[] = "dt_name";
static const char ATTR_PMNC_COUNTERS[] = "pmnc_counters";
static const char ATTR_HAS_CYCLES_COUNTER[] = "has_cycles_counter";

PmuXML::PmuXML()
{
}

PmuXML::~PmuXML()
{
}

void PmuXML::read(const char * const path)
{
    {
        const char *xml;
        unsigned int len;
        getDefaultXml(&xml, &len);
        parse(xml);
    }

    if (path != NULL) {
        // Parse user defined items second as they will show up first in the linked list
        char *xml = readFromDisk(path);
        if (xml == NULL) {
            logg.logError("Unable to open additional pmus XML %s", path);
            handleException();
        }
        parse(xml);
        free(xml);
    }
}

void PmuXML::parse(const char * const xml)
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
        const char * const pmncCountersStr = mxmlElementGetAttr(node, ATTR_PMNC_COUNTERS);
        int pmncCounters;
        if (!stringToInt(&pmncCounters, pmncCountersStr, 0)) {
            logg.logError("The pmnc_counters for '%s' in pmu XML is not an integer", pmncName);
            handleException();
        }
        if (pmncName == NULL || cpuid == 0 || coreName == NULL || pmncCounters == 0) {
            logg.logError("A pmu from the pmu XML is missing one or more of the required attributes (%s, %s, %s and %s)", ATTR_PMNC_NAME, ATTR_CPUID, ATTR_CORE_NAME, ATTR_PMNC_COUNTERS);
            handleException();
        }
        new GatorCpu(strdup(coreName), strdup(pmncName), dtName == NULL ? NULL : strdup(dtName), cpuid, pmncCounters);
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
        new UncorePmu(strdup(coreName), strdup(pmncName), pmncCounters, hasCyclesCounter);
    }

    mxmlDelete(root);
}

void PmuXML::getDefaultXml(const char ** const xml, unsigned int * const len)
{
#include "pmus_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len
    *xml = reinterpret_cast<const char *>(pmus_xml);
    *len = pmus_xml_len;
}

void PmuXML::writeToKernel()
{
    char buf[512];

    for (GatorCpu *gatorCpu = GatorCpu::getHead(); gatorCpu != NULL; gatorCpu = gatorCpu->getNext()) {
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s", gatorCpu->getPmncName());
        if (access(buf, X_OK) == 0) {
            continue;
        }
        DriverSource::writeDriver("/dev/gator/pmu/export", gatorCpu->getPmncName());
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/cpuid", gatorCpu->getPmncName());
        DriverSource::writeDriver(buf, gatorCpu->getCpuid());
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/core_name", gatorCpu->getPmncName());
        DriverSource::writeDriver(buf, gatorCpu->getCoreName());
        if (gatorCpu->getDtName() != NULL) {
            snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/dt_name", gatorCpu->getPmncName());
            DriverSource::writeDriver(buf, gatorCpu->getDtName());
        }
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/pmnc_counters", gatorCpu->getPmncName());
        DriverSource::writeDriver(buf, gatorCpu->getPmncCounters());
    }

    for (UncorePmu *uncorePmu = UncorePmu::getHead(); uncorePmu != NULL; uncorePmu = uncorePmu->getNext()) {
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s", uncorePmu->getPmncName());
        if (access(buf, X_OK) == 0) {
            continue;
        }
        DriverSource::writeDriver("/dev/gator/uncore_pmu/export", uncorePmu->getPmncName());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/core_name", uncorePmu->getPmncName());
        DriverSource::writeDriver(buf, uncorePmu->getCoreName());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/pmnc_counters", uncorePmu->getPmncName());
        DriverSource::writeDriver(buf, uncorePmu->getPmncCounters());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/has_cycles_counter", uncorePmu->getPmncName());
        DriverSource::writeDriver(buf, uncorePmu->getHasCyclesCounter());
    }

    DriverSource::writeDriver("/dev/gator/pmu_init", "1");

    // Was any CPU detected?
    bool foundCpu = false;
    for (GatorCpu *gatorCpu = GatorCpu::getHead(); gatorCpu != NULL; gatorCpu = gatorCpu->getNext()) {
        snprintf(buf, sizeof(buf), "/dev/gator/events/%s_cnt0", gatorCpu->getPmncName());
        if (access(buf, X_OK) == 0) {
            foundCpu = true;
            break;
        }
    }

    if (!foundCpu) {
        logCpuNotFound();
    }

    {
        DIR *dir = opendir("/dev/gator/clusters");

        if (dir == NULL) {
            logg.logError("Unable to open /dev/gator/clusters");
            handleException();
        }

        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            GatorCpu *gatorCpu = GatorCpu::find(dirent->d_name);
            if (gatorCpu != NULL) {
                snprintf(buf, sizeof(buf), "/dev/gator/clusters/%s", dirent->d_name);
                int clusterId;
                if (DriverSource::readIntDriver(buf, &clusterId)) {
                    logg.logError("Unable to read cluster id");
                    handleException();
                }
                gSessionData.mSharedData->mClusters[clusterId] = gatorCpu;
                gSessionData.mSharedData->mClusterCount = std::max(gSessionData.mSharedData->mClusterCount, clusterId + 1);
            }
        }

        closedir(dir);

        gSessionData.updateClusterIds();
    }
}
