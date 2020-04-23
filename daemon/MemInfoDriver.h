/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef MEMINFODRIVER_H
#define MEMINFODRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class MemInfoDriver : public PolledDriver {
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
    MemInfoDriver(const MemInfoDriver &) = delete;
    MemInfoDriver & operator=(const MemInfoDriver &) = delete;
    MemInfoDriver(MemInfoDriver &&) = delete;
    MemInfoDriver & operator=(MemInfoDriver &&) = delete;
};

#endif // MEMINFODRIVER_H
