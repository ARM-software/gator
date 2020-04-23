/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef CCNDRIVER_H
#define CCNDRIVER_H

#include "Driver.h"

#include <string>

class CCNDriver : public Driver {
public:
    CCNDriver();
    ~CCNDriver();

    bool claimCounter(Counter & counter) const;
    void resetCounters();
    void setupCounter(Counter & counter);

    void readEvents(mxml_node_t * const);
    int writeCounters(mxml_node_t * const root) const;
    void writeEvents(mxml_node_t * const) const;

    std::string validateCounters() const;

private:
    enum NodeType {
        NT_UNKNOWN,
        NT_HNF,
        NT_RNI,
        NT_SBAS,
    };

    NodeType * mNodeTypes;
    int mXpCount;

    // Intentionally unimplemented
    CCNDriver(const CCNDriver &) = delete;
    CCNDriver & operator=(const CCNDriver &) = delete;
    CCNDriver(CCNDriver &&) = delete;
    CCNDriver & operator=(CCNDriver &&) = delete;
};

#endif // CCNDRIVER_H
