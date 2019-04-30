/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_
#define NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_

#include "ClassBoilerPlate.h"
#include "Driver.h"
#include "DriverCounter.h"

class SimpleDriver : public Driver
{
public:
    virtual ~SimpleDriver();

    bool claimCounter(Counter &counter) const;
    bool countersEnabled() const;
    void resetCounters();
    void setupCounter(Counter &counter);
    int writeCounters(mxml_node_t *root) const;

protected:
    SimpleDriver(const char * name)
            : Driver(name),
              mCounters(NULL)
    {
    }

    DriverCounter *getCounters() const
    {
        return mCounters;
    }

    void setCounters(DriverCounter * const counter)
    {
        mCounters = counter;
    }

    DriverCounter *findCounter(Counter &counter) const;

private:
    DriverCounter *mCounters;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SimpleDriver)
    ;
};

#endif /* NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_ */
