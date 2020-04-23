/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_
#define NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_

#include "SimpleDriver.h"

class Buffer;

class PolledDriver : public SimpleDriver {
public:
    virtual ~PolledDriver();

    virtual void start() {}
    virtual void read(Buffer * const buffer);

protected:
    PolledDriver(const char * name) : SimpleDriver(name) {}

private:
    // Intentionally unimplemented
    PolledDriver(const PolledDriver &) = delete;
    PolledDriver & operator=(const PolledDriver &) = delete;
    PolledDriver(PolledDriver &&) = delete;
    PolledDriver & operator=(PolledDriver &&) = delete;
};

#endif /* NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_ */
