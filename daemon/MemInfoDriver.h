/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef MEMINFODRIVER_H
#define MEMINFODRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class MemInfoDriver : public PolledDriver {
private:
    using super = PolledDriver;

public:
    MemInfoDriver() : PolledDriver("MemInfo") {}

    // Intentionally unimplemented
    MemInfoDriver(const MemInfoDriver &) = delete;
    MemInfoDriver & operator=(const MemInfoDriver &) = delete;
    MemInfoDriver(MemInfoDriver &&) = delete;
    MemInfoDriver & operator=(MemInfoDriver &&) = delete;

    void readEvents(mxml_node_t * root) override;
    void read(IBlockCounterFrameBuilder & buffer) override;

private:
    DynBuf mBuf {};
    int64_t mMemUsed {0};
    int64_t mMemFree {0};
    int64_t mBuffers {0};
    int64_t mCached {0};
    int64_t mSlab {0};
};

#endif // MEMINFODRIVER_H
