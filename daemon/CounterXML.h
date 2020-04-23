/* Copyright (C) 2011-2020 by Arm Limited. All rights reserved. */

#ifndef COUNTERXML_H_
#define COUNTERXML_H_

#include "lib/Span.h"

#include <memory>

class Driver;
class ICpuInfo;

namespace counters_xml {
    std::unique_ptr<char, void (*)(void *)> getXML(bool supportsMultiEbs,
                                                   lib::Span<const Driver * const> drivers,
                                                   const ICpuInfo & cpuInfo);

    void write(const char * path,
               bool supportsMultiEbs,
               lib::Span<const Driver * const> drivers,
               const ICpuInfo & cpuInfo);
};
#endif /* COUNTERXML_H_ */
