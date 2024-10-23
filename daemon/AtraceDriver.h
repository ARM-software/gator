/* Copyright (C) 2015-2024 by Arm Limited. All rights reserved. */

#ifndef ATRACEDRIVER_H
#define ATRACEDRIVER_H

#include "SimpleDriver.h"

#include <mxml.h>

class FtraceDriver;

class AtraceDriver : public SimpleDriver {
public:
    AtraceDriver(const FtraceDriver & ftraceDriver);

    // Intentionally unimplemented
    AtraceDriver(const AtraceDriver &) = delete;
    AtraceDriver & operator=(const AtraceDriver &) = delete;
    AtraceDriver(AtraceDriver &&) = delete;
    AtraceDriver & operator=(AtraceDriver &&) = delete;

    void readEvents(mxml_node_t * xml) override;

    void start();
    void stop();

    [[nodiscard]] bool isSupported() const { return mSupported; }

private:
    void setAtrace(int flags);

    bool mSupported;
    bool isATraceEnabled {false};
    char mNotifyPath[256];
    const FtraceDriver & mFtraceDriver;
};

#endif // ATRACEDRIVER_H
