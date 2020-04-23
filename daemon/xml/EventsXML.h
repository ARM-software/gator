/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef EVENTS_XML_H
#define EVENTS_XML_H

#include "lib/Span.h"
#include "mxml/mxml.h"

#include <memory>
class Driver;
class GatorCpu;

namespace events_xml {
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getTree(lib::Span<const GatorCpu> clusters);

    std::unique_ptr<char, void (*)(void *)> getXML(lib::Span<const Driver * const> drivers,
                                                   lib::Span<const GatorCpu> clusters);

    void write(const char * path, lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters);
};

#endif // EVENTS_XML_H
