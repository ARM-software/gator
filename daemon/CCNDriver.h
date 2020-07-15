/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef CCNDRIVER_H
#define CCNDRIVER_H

#include "Driver.h"

#include <string>

class CCNDriver : public Driver {
public:
    CCNDriver();
    ~CCNDriver() override;

    bool claimCounter(Counter & counter) const override;
    void resetCounters() override;
    void setupCounter(Counter & counter) override;

    void readEvents(mxml_node_t * const /*unused*/) override;
    int writeCounters(mxml_node_t * root) const override;
    void writeEvents(mxml_node_t * const /*unused*/) const override;

    static std::string validateCounters();

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
