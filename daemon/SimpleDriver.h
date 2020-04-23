/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_
#define NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_

#include "Driver.h"
#include "DriverCounter.h"

class SimpleDriver : public Driver {
public:
    virtual ~SimpleDriver();

    bool claimCounter(Counter & counter) const;
    bool countersEnabled() const;
    void resetCounters();
    void setupCounter(Counter & counter);
    int writeCounters(mxml_node_t * root) const;

protected:
    SimpleDriver(const char * name) : Driver(name), mCounters(NULL) {}

    DriverCounter * getCounters() const { return mCounters; }

    void setCounters(DriverCounter * const counter) { mCounters = counter; }

    DriverCounter * findCounter(Counter & counter) const;

private:
    DriverCounter * mCounters;

    // Intentionally unimplemented
    SimpleDriver(const SimpleDriver &) = delete;
    SimpleDriver & operator=(const SimpleDriver &) = delete;
    SimpleDriver(SimpleDriver &&) = delete;
    SimpleDriver & operator=(SimpleDriver &&) = delete;
};

#endif /* NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_ */
