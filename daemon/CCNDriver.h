/* Copyright (C) 2014-2024 by Arm Limited. All rights reserved. */

#ifndef CCNDRIVER_H
#define CCNDRIVER_H

#include "Driver.h"

#include <string>

class CCNDriver : public Driver {
public:
    CCNDriver();
    ~CCNDriver() override;

    // Intentionally unimplemented
    CCNDriver(const CCNDriver &) = delete;
    CCNDriver & operator=(const CCNDriver &) = delete;
    CCNDriver(CCNDriver &&) = delete;
    CCNDriver & operator=(CCNDriver &&) = delete;

    [[nodiscard]] bool claimCounter(Counter & counter) const override;
    void resetCounters() override;
    void setupCounter(Counter & counter) override;

    void readEvents(mxml_node_t * root) override;
    [[nodiscard]] int writeCounters(available_counter_consumer_t const & consumer) const override;
    void writeEvents(mxml_node_t * root) const override;

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
};

#endif // CCNDRIVER_H
