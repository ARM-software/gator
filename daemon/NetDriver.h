/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef NETDRIVER_H
#define NETDRIVER_H

#include "DynBuf.h"
#include "PolledDriver.h"

class NetDriver : public PolledDriver {
private:
    using super = PolledDriver;

public:
    NetDriver() : PolledDriver("Net") {}

    // Intentionally unimplemented
    NetDriver(const NetDriver &) = delete;
    NetDriver & operator=(const NetDriver &) = delete;
    NetDriver(NetDriver &&) = delete;
    NetDriver & operator=(NetDriver &&) = delete;

    void readEvents(mxml_node_t * root) override;
    void start() override;
    void read(IBlockCounterFrameBuilder & buffer) override;

private:
    bool doRead();

    DynBuf mBuf {};
    uint64_t mReceiveBytes {0};
    uint64_t mTransmitBytes {0};
};

#endif // NETDRIVER_H
