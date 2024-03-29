/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef HWMONDRIVER_H
#define HWMONDRIVER_H

#include "PolledDriver.h"

class HwmonDriver : public PolledDriver {
public:
    HwmonDriver();
    ~HwmonDriver() override;

    // Intentionally unimplemented
    HwmonDriver(const HwmonDriver &) = delete;
    HwmonDriver & operator=(const HwmonDriver &) = delete;
    HwmonDriver(HwmonDriver &&) = delete;
    HwmonDriver & operator=(HwmonDriver &&) = delete;

    void readEvents(mxml_node_t * root) override;

    void writeEvents(mxml_node_t * root) const override;

    void start() override;
};

#endif // HWMONDRIVER_H
