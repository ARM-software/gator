/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef FSDRIVER_H
#define FSDRIVER_H

#include "PolledDriver.h"

class FSDriver : public PolledDriver {
public:
    FSDriver();

    void readEvents(mxml_node_t * xml) override;

    int writeCounters(mxml_node_t * root) const override;

private:
    // Intentionally unimplemented
    FSDriver(const FSDriver &) = delete;
    FSDriver & operator=(const FSDriver &) = delete;
    FSDriver(FSDriver &&) = delete;
    FSDriver & operator=(FSDriver &&) = delete;
};

#endif // FSDRIVER_H
