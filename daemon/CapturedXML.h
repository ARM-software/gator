/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#ifndef __CAPTURED_XML_H__
#define __CAPTURED_XML_H__

#include "Configuration.h"
#include "lib/Span.h"

#include <map>
#include <memory>
#include <vector>

class PrimarySourceProvider;
struct CapturedSpe;

namespace captured_xml {
    /**
     * @param maliGpuIds map from device number to gpu id
     */
    std::unique_ptr<char, void (*)(void *)> getXML(bool includeTime,
                                                   lib::Span<const CapturedSpe> spes,
                                                   const std::vector<TemplateConfiguration> & templateConfiguration,
                                                   const PrimarySourceProvider & primarySourceProvider,
                                                   const std::map<unsigned, unsigned> & maliGpuIds);
    void write(const char * path,
               lib::Span<const CapturedSpe> spes,
               const std::vector<TemplateConfiguration> & templateConfiguration,
               const PrimarySourceProvider & primarySourceProvider,
               const std::map<unsigned, unsigned> & maliGpuIds);
};

#endif //__CAPTURED_XML_H__
