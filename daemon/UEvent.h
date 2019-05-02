/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef UEVENT_H
#define UEVENT_H

#include "ClassBoilerPlate.h"

struct UEventResult
{
    const char *mAction;
    const char *mDevPath;
    const char *mSubsystem;
    char mBuf[1 << 13];
};

class UEvent
{
public:
    UEvent();
    ~UEvent();

    bool init();
    bool read(UEventResult * const result);

    int getFd() const
    {
        return mFd;
    }

    bool enabled() const
    {
        return mFd >= 0;
    }

private:
    int mFd;

    // Intentionally undefined
    CLASS_DELETE_COPY_MOVE(UEvent);
};

#endif // UEVENT_H
