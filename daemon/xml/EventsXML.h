/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"

#include <memory>

#include <mxml.h>

class Driver;
class GatorCpu;
class UncorePmu;

namespace events_xml {

    /// Gets the events that come from commandline/builtin events.xml
    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getStaticTree(lib::Span<const GatorCpu> clusters,
                                                                        lib::Span<const UncorePmu> uncores);

    /// Gets the events that come from commandline/builtin events.xml plus ones added by drivers
    std::unique_ptr<char, void (*)(void *)> getDynamicXML(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters,
                                                          lib::Span<const UncorePmu> uncores);

    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getDynamicTree(lib::Span<const Driver * const> drivers,
                                                                         lib::Span<const GatorCpu> clusters,
                                                                         lib::Span<const UncorePmu> uncores);

    void write(const char * path,
               lib::Span<const Driver * const> drivers,
               lib::Span<const GatorCpu> clusters,
               lib::Span<const UncorePmu> uncores);
};
