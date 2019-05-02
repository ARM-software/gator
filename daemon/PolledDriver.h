/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_
#define NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"

class Buffer;

class PolledDriver : public SimpleDriver
{
public:
    virtual ~PolledDriver();

    virtual void start()
    {
    }
    virtual void read(Buffer * const buffer);

protected:
    PolledDriver(const char * name) : SimpleDriver(name)
    {
    }

private:
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(PolledDriver);
};

#endif /* NATIVE_GATOR_DAEMON_POLLEDDRIVER_H_ */
