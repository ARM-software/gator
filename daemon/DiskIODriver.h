/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef DISKIODRIVER_H
#define DISKIODRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class DiskIODriver : public PolledDriver {
private:
    using super = PolledDriver;

public:
    DiskIODriver();

    void readEvents(mxml_node_t * root) override;
    void start() override;
    void read(IBlockCounterFrameBuilder & buffer) override;

private:
    void doRead();

    DynBuf mBuf;
    uint64_t mReadBytes;
    uint64_t mWriteBytes;

    // Intentionally unimplemented
    DiskIODriver(const DiskIODriver &) = delete;
    DiskIODriver & operator=(const DiskIODriver &) = delete;
    DiskIODriver(DiskIODriver &&) = delete;
    DiskIODriver & operator=(DiskIODriver &&) = delete;
};

#endif // DISKIODRIVER_H
