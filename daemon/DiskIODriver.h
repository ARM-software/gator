/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

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
    void writeEvents(mxml_node_t * root) const override;

private:
    void doRead();
};

#endif // DISKIODRIVER_H
