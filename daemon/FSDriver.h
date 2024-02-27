/* Copyright (C) 2014-2024 by Arm Limited. All rights reserved. */

#ifndef FSDRIVER_H
#define FSDRIVER_H

#include "PolledDriver.h"

class FSDriver : public PolledDriver {
public:
    FSDriver();
    // Intentionally unimplemented
    FSDriver(const FSDriver &) = delete;
    FSDriver & operator=(const FSDriver &) = delete;
    FSDriver(FSDriver &&) = delete;
    FSDriver & operator=(FSDriver &&) = delete;

    void readEvents(mxml_node_t * xml) override;

    [[nodiscard]] int writeCounters(available_counter_consumer_t const & consumer) const override;
};

#endif // FSDRIVER_H
