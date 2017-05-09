/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef KMOD_H
#define KMOD_H

#include "Driver.h"

// Driver for the gator kernel module
class KMod : public Driver
{
public:
    KMod()
            : mIsMaliCapture(false)
    {
    }
    ~KMod()
    {
    }

    bool claimCounter(const Counter &counter) const;
    void resetCounters();
    void setupCounter(Counter &counter);

    int writeCounters(mxml_node_t *root) const;

    bool isMaliCapture() const
    {
        return mIsMaliCapture;
    }

private:
    bool mIsMaliCapture;

    static bool isMaliCounter(const Counter &counter);
};

#endif // KMOD_H
