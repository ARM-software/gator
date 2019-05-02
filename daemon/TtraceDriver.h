/**
 * Copyright (C) Arm Limited 2015-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TTRACEDRIVER_H
#define TTRACEDRIVER_H

#include "mxml/mxml.h"

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"

class FtraceDriver;

class TtraceDriver : public SimpleDriver
{
public:
    TtraceDriver(const FtraceDriver & ftraceDriver);
    ~TtraceDriver();

    void readEvents(mxml_node_t * const xml);

    void start();
    void stop();

    bool isSupported() const
    {
        return mSupported;
    }

private:
    void setTtrace(const int flags);

    bool mSupported;
    const FtraceDriver & mFtraceDriver;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(TtraceDriver);
};

#endif // TTRACEDRIVER_H
