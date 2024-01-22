/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#include "CounterXML.h"

#include "Driver.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "logging/suppliers.h"
#include "xml/MxmlUtils.h"
#include "xml/PmuXML.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ostream>
#include <sstream>

#include <mxml.h>

static mxml_node_t * getTree(bool supportsMultiEbs,
                             lib::Span<const Driver * const> drivers,
                             const ICpuInfo & cpuInfo,
                             const logging::log_access_ops_t & log_ops)
{
    auto * const xml = mxmlNewXML("1.0");
    auto * const counters = mxmlNewElement(xml, "counters");

    if (supportsMultiEbs) {
        mxmlElementSetAttr(counters, "supports-multiple-ebs", "yes");
    }

    int count = 0;
    for (const Driver * driver : drivers) {
        count += driver->writeCounters(counters);
    }

    if (count == 0) {
        LOG_ERROR("No counters found, this could be because /dev/gator/events can not be read or because perf is "
                  "not working correctly");
        handleException();
    }

    auto setup_message = log_ops.get_log_setup_messages();
    mxml_node_t * setup = mxmlNewElement(counters, "setup_warnings");
    mxmlNewText(setup, 0, setup_message.c_str());
    {
        std::ostringstream buffer {};
        for (auto const * driver : drivers) {
            auto warnings = driver->get_other_warnings();
            for (auto const & warning : warnings) {
                buffer << warning << "|" << '\n';
            }
        }
        auto * warning_element = mxmlNewElement(counters, "other_warnings");
        mxmlNewText(warning_element, 0, buffer.str().c_str());
    }

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
                                                   const ICpuInfo & cpuInfo,
                                                   const logging::log_access_ops_t & log_ops)
    {
        mxml_node_t * xml = getTree(supportsMultiEbs, drivers, cpuInfo, log_ops);
        auto * const xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
        mxmlDelete(xml);
        return {xml_string, &::free};
    }

    void write(const char * path,
               bool supportsMultiEbs,
               lib::Span<const Driver * const> drivers,
               const ICpuInfo & cpuInfo,
               const logging::log_access_ops_t & log_ops)
    {
        // Set full path
        lib::printf_str_t<PATH_MAX> file {"%s/counters.xml", path};

        if (writeToDisk(file, getXML(supportsMultiEbs, drivers, cpuInfo, log_ops).get()) < 0) {
            LOG_ERROR("Error writing %s\nPlease verify the path.", file.c_str());
            handleException();
        }
    }

}
