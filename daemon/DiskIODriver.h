/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DISKIODRIVER_H
#define DISKIODRIVER_H

#include "ClassBoilerPlate.h"
#include "PolledDriver.h"
#include "DynBuf.h"

class DiskIODriver : public PolledDriver
{
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
    CLASS_DELETE_COPY_MOVE(DiskIODriver);
};

#endif // DISKIODRIVER_H
