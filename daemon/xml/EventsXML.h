/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EVENTS_XML_H
#define EVENTS_XML_H

#include <memory>

#include "lib/Span.h"
#include "mxml/mxml.h"
class Driver;
class GatorCpu;

namespace events_xml
{
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getTree(lib::Span<const GatorCpu> clusters);

    std::unique_ptr<char, void(*)(void*)> getXML(lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters);

    void write(const char* path, lib::Span<const Driver * const > drivers, lib::Span<const GatorCpu> clusters);
};

#endif // EVENTS_XML_H
