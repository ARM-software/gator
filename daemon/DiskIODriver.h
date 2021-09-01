/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef DISKIODRIVER_H
#define DISKIODRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class DiskIODriver : public PolledDriver {
private:
    using super = PolledDriver;

public:
    DiskIODriver() : PolledDriver("DiskIO") {}

    // Intentionally unimplemented
    DiskIODriver(const DiskIODriver &) = delete;
    DiskIODriver & operator=(const DiskIODriver &) = delete;
    DiskIODriver(DiskIODriver &&) = delete;
    DiskIODriver & operator=(DiskIODriver &&) = delete;

    void readEvents(mxml_node_t * root) override;
    void start() override;
    void read(IBlockCounterFrameBuilder & buffer) override;

private:
    void doRead();

    DynBuf mBuf {};
    uint64_t mReadBytes {0};
    uint64_t mWriteBytes {0};
};

#endif // DISKIODRIVER_H
