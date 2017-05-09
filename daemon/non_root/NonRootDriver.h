/* Copyright (c) 2017 by ARM Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_NONROOTDRIVER_H
#define INCLUDE_NON_ROOT_NONROOTDRIVER_H

#include "SimpleDriver.h"
#include "non_root/NonRootCounter.h"

#include <map>

class GatorCpu;

namespace non_root
{
    /**
     * Non-root Capture driver
     */
    class NonRootDriver : public SimpleDriver
    {
    public:

        NonRootDriver();

        virtual void readEvents(mxml_node_t * const) override;
        virtual void writeEvents(mxml_node_t * const) const override;

        std::map<NonRootCounter, int> getEnabledCounters() const;

    private:

        void addCluster(GatorCpu * gatorCpu);
    };
}

#endif /* INCLUDE_NON_ROOT_NONROOTDRIVER_H */
