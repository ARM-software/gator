/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "CounterXML.h"

#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "Driver.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "PmuXML.h"
#include "mxml/mxml.h"
#include "SessionData.h"

static mxml_node_t* getTree(lib::Span<const Driver * const > drivers, const ICpuInfo & cpuInfo)
{
    mxml_node_t *xml;
    mxml_node_t *counters;

    xml = mxmlNewXML("1.0");
    counters = mxmlNewElement(xml, "counters");
    int count = 0;
    for (const Driver *driver : drivers) {
        count += driver->writeCounters(counters);
    }

    if (count == 0) {
        logg.logError("No counters found, this could be because /dev/gator/events can not be read or because perf is not working correctly");
        handleException();
    }

    mxml_node_t *setup = mxmlNewElement(counters, "setup_warnings");
    mxmlNewText(setup, 0, logg.getSetup());

    // always send the cluster information; even on devices where not all the information is available.
    for (size_t cluster = 0; cluster < cpuInfo.getClusters().size(); ++cluster) {
        mxml_node_t *node = mxmlNewElement(counters, "cluster");
        mxmlElementSetAttrf(node, "id", "%zi", cluster);
        mxmlElementSetAttr(node, "name", cpuInfo.getClusters()[cluster].getPmncName());
    }
    for (size_t cpu = 0; cpu < cpuInfo.getClusterIds().size(); ++cpu) {
        if (cpuInfo.getClusterIds()[cpu] >= 0) {
            mxml_node_t *node = mxmlNewElement(counters, "cpu");
            mxmlElementSetAttrf(node, "id", "%zu", cpu);
            mxmlElementSetAttrf(node, "cluster", "%i", cpuInfo.getClusterIds()[cpu]);
        }
    }
    return xml;
}

namespace counters_xml
{

    std::unique_ptr<char, void (*)(void*)> getXML(lib::Span<const Driver * const > drivers, const ICpuInfo & cpuInfo)
    {
        char* xml_string;
        mxml_node_t *xml = getTree(drivers, cpuInfo);
        xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
        mxmlDelete(xml);
        return {xml_string, &::free};
    }

    void write(const char* path, lib::Span<const Driver * const > drivers, const ICpuInfo & cpuInfo)
    {
        char file[PATH_MAX];

        // Set full path
        snprintf(file, PATH_MAX, "%s/counters.xml", path);

        if (writeToDisk(file, getXML(drivers, cpuInfo).get()) < 0) {
            logg.logError("Error writing %s\nPlease verify the path.", file);
            handleException();
        }
    }

}
