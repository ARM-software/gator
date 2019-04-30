/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CCNDRIVER_H
#define CCNDRIVER_H

#include <string>

#include "ClassBoilerPlate.h"
#include "Driver.h"

class CCNDriver : public Driver
{
public:
    CCNDriver();
    ~CCNDriver();

    bool claimCounter(Counter &counter) const;
    void resetCounters();
    void setupCounter(Counter &counter);

    void readEvents(mxml_node_t * const);
    int writeCounters(mxml_node_t * const root) const;
    void writeEvents(mxml_node_t * const) const;

    std::string validateCounters() const;

private:
    enum NodeType
    {
        NT_UNKNOWN,
        NT_HNF,
        NT_RNI,
        NT_SBAS,
    };

    NodeType *mNodeTypes;
    int mXpCount;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(CCNDriver);
};

#endif // CCNDRIVER_H
