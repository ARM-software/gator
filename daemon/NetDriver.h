/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef NETDRIVER_H
#define NETDRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class NetDriver : public PolledDriver {
private:
    typedef PolledDriver super;

public:
    NetDriver();
    ~NetDriver();

    void readEvents(mxml_node_t * const root);
    void start();
    void read(Buffer * const buffer);

private:
    bool doRead();

    DynBuf mBuf;
    uint64_t mReceiveBytes;
    uint64_t mTransmitBytes;

    // Intentionally unimplemented
    NetDriver(const NetDriver &) = delete;
    NetDriver & operator=(const NetDriver &) = delete;
    NetDriver(NetDriver &&) = delete;
    NetDriver & operator=(NetDriver &&) = delete;
};

#endif // NETDRIVER_H
