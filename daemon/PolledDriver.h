/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_
#define NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_

#include "SimpleDriver.h"

class IBlockCounterFrameBuilder;

class PolledDriver : public SimpleDriver {
public:
    // Intentionally unimplemented
    PolledDriver(const PolledDriver &) = delete;
    PolledDriver & operator=(const PolledDriver &) = delete;
    PolledDriver(PolledDriver &&) = delete;
    PolledDriver & operator=(PolledDriver &&) = delete;

    virtual void start() {}
    virtual void read(IBlockCounterFrameBuilder & buffer);

protected:
    PolledDriver(const char * name) : SimpleDriver(name) {}
};

#endif /* NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_ */
