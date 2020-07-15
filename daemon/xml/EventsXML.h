/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef EVENTS_XML_H
#define EVENTS_XML_H

#include "lib/Span.h"
#include "mxml/mxml.h"

#include <map>
#include <memory>
#include <string>

class Driver;
class GatorCpu;

namespace events_xml {
    /// Gets the events that come from commandline/builtin events.xml
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getStaticTree(lib::Span<const GatorCpu> clusters);

    /// Gets the events that come from commandline/builtin events.xml plus ones added by drivers
    std::unique_ptr<char, void (*)(void *)> getDynamicXML(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters);

    std::map<std::string, int> getCounterToEventMap(lib::Span<const Driver * const> drivers,
                                                    lib::Span<const GatorCpu> clusters);

    void write(const char * path, lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters);
};

#endif // EVENTS_XML_H
