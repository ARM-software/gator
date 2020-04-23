/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef DISKIODRIVER_H
#define DISKIODRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class DiskIODriver : public PolledDriver {
private:
    typedef PolledDriver super;

public:
    DiskIODriver();
    ~DiskIODriver();

    void readEvents(mxml_node_t * const root);
    void start();
    void read(Buffer * const buffer);

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
