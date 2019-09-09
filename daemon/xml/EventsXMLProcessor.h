/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef EVENTS_XML_PROCESSOR_H
#define EVENTS_XML_PROCESSOR_H

#include <memory>

#include "lib/Span.h"
#include "xml/MxmlUtils.h"

class GatorCpu;

namespace events_xml
{
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
     *
     * @param xml
     * @param clusters
     */
    void processClusters(mxml_node_t * xml, lib::Span<const GatorCpu> clusters);

    /**
     * Get the events element
     * @param xml
     * @return The element
     */
    mxml_node_t * getEventsElement(mxml_node_t * xml);
}

#endif // EVENTS_XML_PROCESSOR_H
