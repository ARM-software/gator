/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MEMINFODRIVER_H
#define MEMINFODRIVER_H

#include "ClassBoilerPlate.h"
#include "PolledDriver.h"
#include "DynBuf.h"

class MemInfoDriver : public PolledDriver
{
private:
    typedef PolledDriver super;

public:
    MemInfoDriver();
    ~MemInfoDriver();

    void readEvents(mxml_node_t * const root);
    void read(Buffer * const buffer);

private:
    DynBuf mBuf;
    int64_t mMemUsed;
    int64_t mMemFree;
    int64_t mBuffers;
    int64_t mCached;
    int64_t mSlab;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(MemInfoDriver);
};

#endif // MEMINFODRIVER_H
