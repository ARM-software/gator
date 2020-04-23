/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "CounterXML.h"

#include "Driver.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "xml/MxmlUtils.h"
#include "xml/PmuXML.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static mxml_node_t * getTree(bool supportsMultiEbs, lib::Span<const Driver * const> drivers, const ICpuInfo & cpuInfo)
{
    mxml_node_t * xml;
    mxml_node_t * counters;

    xml = mxmlNewXML("1.0");
    counters = mxmlNewElement(xml, "counters");

    if (supportsMultiEbs) {
        mxmlElementSetAttr(counters, "supports-multiple-ebs", "yes");
    }

    int count = 0;
    for (const Driver * driver : drivers) {
        count += driver->writeCounters(counters);
    }

    if (count == 0) {
        logg.logError("No counters found, this could be because /dev/gator/events can not be read or because perf is "
                      "not working correctly");
        handleException();
    }

    mxml_node_t * setup = mxmlNewElement(counters, "setup_warnings");
    mxmlNewText(setup, 0, logg.getSetup());

    // always send the cluster information; even on devices where not all the information is available.
    for (size_t cluster = 0; cluster < cpuInfo.getClusters().size(); ++cluster) {
        mxml_node_t * node = mxmlNewElement(counters, "cluster");
        mxmlElementSetAttrf(node, "id", "%zi", cluster);
        mxmlElementSetAttr(node, "name", cpuInfo.getClusters()[cluster].getId());
    }
    for (size_t cpu = 0; cpu < cpuInfo.getClusterIds().size(); ++cpu) {
        if (cpuInfo.getClusterIds()[cpu] >= 0) {
            mxml_node_t * node = mxmlNewElement(counters, "cpu");
            mxmlElementSetAttrf(node, "id", "%zu", cpu);
            mxmlElementSetAttrf(node, "cluster", "%i", cpuInfo.getClusterIds()[cpu]);
        }
    }
    return xml;
}

namespace counters_xml {

    std::unique_ptr<char, void (*)(void *)> getXML(bool supportsMultiEbs,
                                                   lib::Span<const Driver * const> drivers,
                                                   const ICpuInfo & cpuInfo)
    {
        char * xml_string;
        mxml_node_t * xml = getTree(supportsMultiEbs, drivers, cpuInfo);
        xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
        mxmlDelete(xml);
        return {xml_string, &::free};
    }

    void write(const char * path,
               bool supportsMultiEbs,
               lib::Span<const Driver * const> drivers,
               const ICpuInfo & cpuInfo)
    {
        char file[PATH_MAX];

        // Set full path
        snprintf(file, PATH_MAX, "%s/counters.xml", path);

        if (writeToDisk(file, getXML(supportsMultiEbs, drivers, cpuInfo).get()) < 0) {
            logg.logError("Error writing %s\nPlease verify the path.", file);
            handleException();
        }
    }

}
