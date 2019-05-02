/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_
#define NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_

#include <string>

#include "ClassBoilerPlate.h"
#include "Driver.h"

class DriverCounter
{
public:
    /**
     *
     * @param next
     * @param name will be copied
     */
    DriverCounter(DriverCounter * const next, const char * const name);
    virtual ~DriverCounter() = default;

    DriverCounter *getNext() const
    {
        return mNext;
    }
    const char *getName() const
    {
        return mName.c_str();
    }
    int getKey() const
    {
        return mKey;
    }
    bool isEnabled() const
    {
        return mEnabled;
    }
    void setEnabled(const bool enabled)
    {
        mEnabled = enabled;
    }
    virtual int64_t read()
    {
        return -1;
    }

private:
    DriverCounter * const mNext;
    const std::string mName;
    const int mKey;
    bool mEnabled;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(DriverCounter);
};

#endif /* NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_ */
