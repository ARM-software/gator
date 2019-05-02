/**
 * Copyright (C) Arm Limited 2011-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTERXML_H_
#define COUNTERXML_H_

#include <memory>

#include "lib/Span.h"

class Driver;
class ICpuInfo;

namespace counters_xml
{
    std::unique_ptr<char, void (*)(void*)> getXML(lib::Span<const Driver * const> drivers, const ICpuInfo & cpuInfo);

    void write(const char* path, lib::Span<const Driver * const> drivers, const ICpuInfo & cpuInfo);
};
#endif /* COUNTERXML_H_ */
