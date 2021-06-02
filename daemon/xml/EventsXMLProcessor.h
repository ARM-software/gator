/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef EVENTS_XML_PROCESSOR_H
#define EVENTS_XML_PROCESSOR_H

#include "Events.h"
#include "lib/Optional.h"
#include "lib/Span.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "xml/MxmlUtils.h"

#include <memory>
#include <tuple>

class GatorCpu;
class UncorePmu;

namespace events_xml {
    /**
     * Merge the 'append' tree into the 'main' tree
     *
     * @param mainXml The main tree, which will be modified
     * @param appendXml The additional items to append
     * @return True if successful, false if error
     */
    bool mergeTrees(mxml_node_t * mainXml, mxml_unique_ptr appendXml);

    /**
     * Inject dynamic counter sets based on detected clusters
     */
    void processClusters(mxml_node_t * xml, lib::Span<const GatorCpu> clusters, lib::Span<const UncorePmu> uncores);

    /**
     * Get the events element
     * @return The element
     */
    mxml_node_t * getEventsElement(mxml_node_t * xml);

    /**
     * Create a category node and the matching counter set if needed
     * @return a pair where the first element is the category and the second is the possibly null counter set
     */
    std::pair<mxml_unique_ptr, mxml_unique_ptr> createCategoryAndCounterSetNodes(const Category & category);
}

#endif // EVENTS_XML_PROCESSOR_H
