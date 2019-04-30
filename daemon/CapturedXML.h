/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CAPTURED_XML_H__
#define __CAPTURED_XML_H__

#include <map>
#include <memory>

#include "lib/Span.h"

class PrimarySourceProvider;
struct CapturedSpe;


namespace captured_xml
{
    /**
     *
     * @param includeTime
     * @param primarySourceProvider
     * @param maliGpuIds map from device number to gpu id
     * @return
     */
    std::unique_ptr<char, void (*)(void *)> getXML(bool includeTime, lib::Span<const CapturedSpe> spes, const PrimarySourceProvider & primarySourceProvider,
                                                   const std::map<unsigned, unsigned> & maliGpuIds);
    void write(const char* path, lib::Span<const CapturedSpe> spes, const PrimarySourceProvider & primarySourceProvider,
               const std::map<unsigned, unsigned> & maliGpuIds);
};

#endif //__CAPTURED_XML_H__
