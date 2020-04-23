/* Copyright (C) 2015-2020 by Arm Limited. All rights reserved. */

#ifndef ATRACEDRIVER_H
#define ATRACEDRIVER_H

#include "SimpleDriver.h"
#include "mxml/mxml.h"

class FtraceDriver;

class AtraceDriver : public SimpleDriver {
public:
    AtraceDriver(const FtraceDriver & ftraceDriver);
    ~AtraceDriver();

    void readEvents(mxml_node_t * const xml);

    void start();
    void stop();

    bool isSupported() const { return mSupported; }

private:
    void setAtrace(const int flags);

    bool mSupported;
    char mNotifyPath[256];
    const FtraceDriver & mFtraceDriver;

    // Intentionally unimplemented
    AtraceDriver(const AtraceDriver &) = delete;
    AtraceDriver & operator=(const AtraceDriver &) = delete;
    AtraceDriver(AtraceDriver &&) = delete;
    AtraceDriver & operator=(AtraceDriver &&) = delete;
};

#endif // ATRACEDRIVER_H
