/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "KMod.h"

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "ConfigurationXML.h"
#include "Counter.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/Utils.h"
#include "linux/perf/PerfUtils.h"

static const char ARM_MALI_MIDGARD[] = "ARM_Mali-Midgard_";
static const char ARM_MALI_T[] = "ARM_Mali-T";
static const char ARM_MALI_BIFROST[] = "ARM_Mali-Bifrost_";

bool KMod::isMaliCounter(const Counter &counter)
{
    return ((strncmp(counter.getType(), ARM_MALI_MIDGARD, sizeof(ARM_MALI_MIDGARD) - 1) == 0)
            || (strncmp(counter.getType(), ARM_MALI_BIFROST, sizeof(ARM_MALI_BIFROST) - 1) == 0)
            || (strncmp(counter.getType(), ARM_MALI_T, sizeof(ARM_MALI_T) - 1) == 0));
}

// Claim all the counters in /dev/gator/events
bool KMod::claimCounter(Counter &counter) const
{
    if (isMaliCounter(counter) && (counter.getDriver() != NULL)) {
        // do not claim if another driver has claimed this mali counter
        return false;
    }

    char text[512];
    snprintf(text, sizeof(text), "/dev/gator/events/%s", counter.getType());
    return access(text, F_OK) == 0;
}

void KMod::resetCounters()
{
    char base[384];
    char text[512];

    // Initialize all perf counters in the driver, i.e. set enabled to zero
    struct dirent *ent;
    DIR* dir = opendir("/dev/gator/events");
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            // skip hidden files, current dir, and parent dir
            if (ent->d_name[0] == '.')
                continue;
            snprintf(base, sizeof(base), "/dev/gator/events/%s", ent->d_name);
            snprintf(text, sizeof(text), "%s/enabled", base);
            lib::writeIntToFile(text, 0);
            snprintf(text, sizeof(text), "%s/count", base);
            lib::writeIntToFile(text, 0);
        }
        closedir(dir);
    }
}

void KMod::setupCounter(Counter &counter)
{
    char base[384];
    char text[512];
    snprintf(base, sizeof(base), "/dev/gator/events/%s", counter.getType());

    if (isMaliCounter(counter)) {
        mIsMaliCapture = true;
    }

    snprintf(text, sizeof(text), "%s/enabled", base);
    int enabled = true;
    if (lib::writeReadIntInFile(text, enabled) || !enabled) {
        counter.setEnabled(false);
        return;
    }

    int value = 0;
    snprintf(text, sizeof(text), "%s/key", base);
    lib::readIntFromFile(text, value);
    counter.setKey(value);

    snprintf(text, sizeof(text), "%s/cores", base);
    if (lib::readIntFromFile(text, value) == 0) {
        counter.setCores(value);
    }

    snprintf(text, sizeof(text), "%s/event", base);
    lib::writeIntToFile(text, counter.getEvent());
    snprintf(text, sizeof(text), "%s/count", base);
    if (access(text, F_OK) == 0) {
        int count = counter.getCount();
        if (lib::writeReadIntInFile(text, count) && counter.getCount() > 0) {
            logg.logError("Cannot enable EBS for %s:%i with a count of %d", counter.getType(), counter.getEvent(), counter.getCount());
            handleException();
        }
        counter.setCount(count);
    }
    else if (counter.getCount() > 0) {
        configuration_xml::remove();
        logg.logError("Event Based Sampling is only supported with kernel versions 3.0.0 and higher with CONFIG_PERF_EVENTS=y, and CONFIG_HW_PERF_EVENTS=y. The invalid configuration.xml has been removed.");
        handleException();
    }
}

int KMod::writeCounters(mxml_node_t *root) const
{
    struct dirent *ent;
    mxml_node_t *counter;

    // counters.xml is simply a file listing of /dev/gator/events
    DIR* dir = opendir("/dev/gator/events");
    if (dir == NULL) {
        return 0;
    }

    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        // skip hidden files, current dir, and parent dir
        if (ent->d_name[0] == '.')
            continue;
        counter = mxmlNewElement(root, "counter");
        mxmlElementSetAttr(counter, "name", ent->d_name);
        ++count;
    }
    closedir(dir);

    return count;
}

void KMod::checkVersion()
{
    int driverVersion = 0;

    if (lib::readIntFromFile("/dev/gator/version", driverVersion) == -1) {
        logg.logError("Error reading gator driver version");
        handleException();
    }

    // Verify the driver version matches the daemon version
    if (driverVersion != PROTOCOL_VERSION) {
        if ((driverVersion > PROTOCOL_DEV) || (PROTOCOL_VERSION > PROTOCOL_DEV)) {
            // One of the mismatched versions is development version
            logg.logError(
                    "DEVELOPMENT BUILD MISMATCH: gator driver version \"%d\" is not in sync with gator daemon version \"%d\".\n"
                    ">> The following must be synchronized from engineering repository:\n"
                    ">> * gator driver\n"
                    ">> * gator daemon\n"
                    ">> * Streamline", driverVersion, PROTOCOL_VERSION);
            handleException();
        }
        else {
            // Release version mismatch
            logg.logError(
                    "gator driver version \"%d\" is different than gator daemon version \"%d\".\n"
                    ">> Please upgrade the driver and daemon to the latest versions.", driverVersion, PROTOCOL_VERSION);
            handleException();
        }
    }
}


std::vector<GatorCpu> KMod::writePmuXml(const PmuXML & pmuXml)
{
    char buf[512];

    for (const GatorCpu & gatorCpu : pmuXml.cpus) {
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s", gatorCpu.getPmncName());
        if (access(buf, X_OK) == 0) {
            continue;
        }
        lib::writeCStringToFile("/dev/gator/pmu/export", gatorCpu.getPmncName());
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/cpuid", gatorCpu.getPmncName());
        lib::writeIntToFile(buf, gatorCpu.getCpuid());
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/core_name", gatorCpu.getPmncName());
        lib::writeCStringToFile(buf, gatorCpu.getCoreName());
        if (gatorCpu.getDtName() != NULL) {
            snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/dt_name", gatorCpu.getPmncName());
            lib::writeCStringToFile(buf, gatorCpu.getDtName());
        }
        snprintf(buf, sizeof(buf), "/dev/gator/pmu/%s/pmnc_counters", gatorCpu.getPmncName());
        lib::writeIntToFile(buf, gatorCpu.getPmncCounters());
    }

    for (const UncorePmu &uncorePmu : pmuXml.uncores) {
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s", uncorePmu.getPmncName());
        if (access(buf, X_OK) == 0) {
            continue;
        }
        lib::writeCStringToFile("/dev/gator/uncore_pmu/export", uncorePmu.getPmncName());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/core_name", uncorePmu.getPmncName());
        lib::writeCStringToFile(buf, uncorePmu.getCoreName());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/pmnc_counters", uncorePmu.getPmncName());
        lib::writeIntToFile(buf, uncorePmu.getPmncCounters());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/has_cycles_counter", uncorePmu.getPmncName());
        lib::writeIntToFile(buf, uncorePmu.getHasCyclesCounter());
        snprintf(buf, sizeof(buf), "/dev/gator/uncore_pmu/%s/cpumask", uncorePmu.getPmncName());
        for (int cpu : perf_utils::readCpuMask(uncorePmu.getPmncName())) {
            lib::writeIntToFile(buf, cpu);
        }
    }

    lib::writeCStringToFile("/dev/gator/pmu_init", "1");

    // Was any CPU detected?
    bool foundCpu = false;
    for (const GatorCpu & gatorCpu : pmuXml.cpus) {
        snprintf(buf, sizeof(buf), "/dev/gator/events/%s_cnt0", gatorCpu.getPmncName());
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

        std::vector<GatorCpu> cpus;
        struct dirent *dirent;
        while ((dirent = readdir(dir)) != NULL) {
            const GatorCpu *gatorCpu = pmuXml.findCpuByName(dirent->d_name);
            if (gatorCpu != NULL) {
                snprintf(buf, sizeof(buf), "/dev/gator/clusters/%s", dirent->d_name);
                // We read the ID but ignore it because it is sorted?
                int clusterId;
                if (lib::readIntFromFile(buf, clusterId)) {
                    logg.logError("Unable to read cluster id");
                    handleException();
                }
                cpus.push_back(*gatorCpu);
            }
        }

        closedir(dir);

        return cpus;
    }
}
