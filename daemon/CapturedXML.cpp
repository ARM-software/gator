/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "CapturedXML.h"

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <cassert>

#include <algorithm>
#include <set>

#include "CapturedSpe.h"
#include "ICpuInfo.h"
#include "PrimarySourceProvider.h"
#include "SessionData.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "mxml/mxml.h"
#include "lib/FsEntry.h"

/* Basic target OS detection */
#undef GATOR_TARGET_OS
#undef GATOR_TARGET_OS_VERSION
#undef GATOR_TARGET_OS_VERSION_FMT
#undef GATOR_TARGET_OS_PROBE

// android NDK build
#if defined(__ANDROID__) && defined(__BIONIC__)
#   define GATOR_TARGET_OS                  "android"
#   if defined(__ANDROID_API__)
#       define GATOR_TARGET_OS_VERSION      __ANDROID_API__
#       define GATOR_TARGET_OS_VERSION_FMT  "%d"
#   endif
// not an android NDK build
#else
#   include <limits.h>
#   if defined(__GLIBC__) || defined(__GNU_LIBRARY__) || defined(__UCLIBC__)
//      using GLIBC or UCLIBC so must be linux
#       define GATOR_TARGET_OS              "linux"
#   elif defined(__linux__)
//      not sure what it is using, probably is musl libc so have to probe filesystem
#       define GATOR_TARGET_OS              detectOs()
#       define GATOR_TARGET_OS_PROBE        1
#   else
//      not linux, hmm maybe tests?
#       define GATOR_TARGET_OS              "unknown"
#   endif
#endif

#if defined(GATOR_TARGET_OS_PROBE)
/** Very simple attempt to detect android/linux by probing fs */
static const char * detectOs()
{
    // maybe musl libc statically linked gatord: probe the filesystem
    lib::FsEntry app_process = lib::FsEntry::create("/system/bin/app_process");
    if (app_process.exists())
        return "android";

    app_process = lib::FsEntry::create("/system/bin/app_process32");
    if (app_process.exists())
        return "android";

    app_process = lib::FsEntry::create("/system/bin/app_process64");
    if (app_process.exists())
        return "android";

    return "linux";
}
#endif

/** Generate the xml tree for capture.xml */
static mxml_node_t* getTree(bool includeTime, lib::Span<const CapturedSpe> spes, const PrimarySourceProvider & primarySourceProvider, const std::map<unsigned, unsigned> & maliGpuIds)
{
    mxml_node_t *xml;
    mxml_node_t *captured;
    mxml_node_t *target;
    int x;

    xml = mxmlNewXML("1.0");

    captured = mxmlNewElement(xml, "captured");
    mxmlElementSetAttr(captured, "version", "1");
    mxmlElementSetAttr(captured, "backtrace_processing", (gSessionData.mBacktraceDepth > 0) ? primarySourceProvider.getBacktraceProcessingMode()
                                                                                            : "none");
    mxmlElementSetAttr(captured, "type", primarySourceProvider.getCaptureXmlTypeValue());
    mxmlElementSetAttrf(captured, "protocol", "%d", PROTOCOL_VERSION);
    if (includeTime) { // Send the following only after the capture is complete
        if (time(NULL) > 1267000000) { // If the time is reasonable (after Feb 23, 2010)
            mxmlElementSetAttrf(captured, "created", "%lu", time(NULL)); // Valid until the year 2038
        }
    }

    target = mxmlNewElement(captured, "target");
    mxmlElementSetAttrf(target, "sample_rate", "%d", gSessionData.mSampleRate);
    const auto & cpuInfo = primarySourceProvider.getCpuInfo();
    mxmlElementSetAttr(target, "name", cpuInfo.getModelName());
    const auto cpuIds = cpuInfo.getCpuIds();
    mxmlElementSetAttrf(target, "cores", "%zu", cpuIds.size());
    //GPU cores
    mxmlElementSetAttrf(target, "gpu_cores", "%zu", maliGpuIds.size());
    //gatord src md5
    mxmlElementSetAttrf(target, "gatord_src_md5sum", "%s", gSrcMd5);
    //gatord build commit id
    mxmlElementSetAttrf(target, "gatord_build_id", "%s", STRIFY(GATORD_BUILD_ID));

    assert(cpuIds.size() > 0); // gatord should've died earlier if there were no cpus
    mxmlElementSetAttrf(target, "cpuid", "0x%x", *std::max_element(begin(cpuIds), end(cpuIds)));

    if (!gSessionData.mOneShot && (gSessionData.mSampleRate > 0)) {
        mxmlElementSetAttr(target, "supports_live", "yes");
    }

    if (gSessionData.mLocalCapture) {
        mxmlElementSetAttr(target, "local_capture", "yes");
    }

    // add some OS information
#if defined(GATOR_TARGET_OS)
    mxmlElementSetAttr(target, "os", GATOR_TARGET_OS);
#   if defined(GATOR_TARGET_OS_VERSION)
    mxmlElementSetAttrf(target, "os_version", GATOR_TARGET_OS_VERSION_FMT, GATOR_TARGET_OS_VERSION);
#   endif
#endif

    // add mali gpu ids
    if (!maliGpuIds.empty())
    {
        // make set of unique ids
        std::set<unsigned> uniqueGpuIds;
        for (auto gpuid : maliGpuIds) {
            uniqueGpuIds.insert(gpuid.second);
        }

        mxml_node_t * const gpuids = mxmlNewElement(captured, "gpus");

        for (unsigned gpuid : uniqueGpuIds) {
            mxml_node_t * const node = mxmlNewElement(gpuids, "gpu");
            mxmlElementSetAttrf(node, "id", "0x%x", gpuid);
        }
    }

    mxml_node_t *counters = NULL;
    for (x = 0; x < MAX_PERFORMANCE_COUNTERS; x++) {
        const Counter & counter = gSessionData.mCounters[x];
        if (counter.isEnabled()) {
            if (counters == NULL) {
                counters = mxmlNewElement(captured, "counters");
            }
            mxml_node_t * const node = mxmlNewElement(counters, "counter");
            mxmlElementSetAttrf(node, "key", "0x%x", counter.getKey());
            mxmlElementSetAttr(node, "type", counter.getType());
            if (counter.getEvent() != -1) {
                mxmlElementSetAttrf(node, "event", "0x%x", counter.getEvent());
            }
            if (counter.getCount() > 0) {
                mxmlElementSetAttrf(node, "count", "%d", counter.getCount());
            }
            if (counter.getCores() > 0) {
                mxmlElementSetAttrf(node, "cores", "%d", counter.getCores());
            }
        }
    }

    for (const auto & spe : spes) {
        if (counters == NULL) {
            counters = mxmlNewElement(captured, "counters");
        }
        mxml_node_t * const node = mxmlNewElement(counters, "spe");
        mxmlElementSetAttrf(node, "key", "0x%x", spe.key);
        mxmlElementSetAttr(node, "id", spe.id.c_str());
    }

    return xml;
}

namespace captured_xml
{
    std::unique_ptr<char, void (*)(void *)> getXML(bool includeTime, lib::Span<const CapturedSpe> spes, const PrimarySourceProvider & primarySourceProvider,
                                                   const std::map<unsigned, unsigned> & maliGpuIds)
    {
        mxml_node_t *xml = getTree(includeTime, spes, primarySourceProvider, maliGpuIds);
        char* xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
        mxmlDelete(xml);
        return {xml_string, &free};
    }

    void write(const char* path, lib::Span<const CapturedSpe> spes, const PrimarySourceProvider & primarySourceProvider,
               const std::map<unsigned, unsigned> & maliGpuIds)
    {
        char file[PATH_MAX];
        // Set full path
        snprintf(file, PATH_MAX, "%s/captured.xml", path);

        if (writeToDisk(file, getXML(true, spes, primarySourceProvider, maliGpuIds).get()) < 0) {
            logg.logError("Error writing %s\nPlease verify the path.", file);
            handleException();
        }
    }
}

