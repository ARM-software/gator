/* Copyright (C) 2015-2020 by Arm Limited. All rights reserved. */

#ifndef TTRACEDRIVER_H
#define TTRACEDRIVER_H

#include "SimpleDriver.h"
#include "mxml/mxml.h"

class FtraceDriver;

class TtraceDriver : public SimpleDriver {
public:
    TtraceDriver(const FtraceDriver & ftraceDriver);
    ~TtraceDriver();

    void readEvents(mxml_node_t * const xml);

    void start();
    void stop();

    bool isSupported() const { return mSupported; }

private:
    void setTtrace(const int flags);

    bool mSupported;
    const FtraceDriver & mFtraceDriver;

    // Intentionally unimplemented
    TtraceDriver(const TtraceDriver &) = delete;
    TtraceDriver & operator=(const TtraceDriver &) = delete;
    TtraceDriver(TtraceDriver &&) = delete;
    TtraceDriver & operator=(TtraceDriver &&) = delete;
};

#endif // TTRACEDRIVER_H
