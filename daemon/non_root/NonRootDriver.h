/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_NONROOTDRIVER_H
#define INCLUDE_NON_ROOT_NONROOTDRIVER_H

#include "SimpleDriver.h"
#include "lib/Span.h"
#include "non_root/NonRootCounter.h"
#include "xml/PmuXML.h"

#include <map>

namespace non_root {
    /**
     * Non-root Capture driver
     */
    class NonRootDriver : public SimpleDriver {
    public:
        NonRootDriver(PmuXML && pmuXml, lib::Span<const GatorCpu> clusters);

        void readEvents(mxml_node_t * const /*unused*/) override;
        void writeEvents(mxml_node_t * const /*unused*/) const override;

        std::map<NonRootCounter, int> getEnabledCounters() const;

        const PmuXML & getPmuXml() const { return pmuXml; }

    private:
        PmuXML pmuXml;
        lib::Span<const GatorCpu> clusters;
    };
}

#endif /* INCLUDE_NON_ROOT_NONROOTDRIVER_H */
