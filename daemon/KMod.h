/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef KMOD_H
#define KMOD_H

#include <vector>

#include "Driver.h"
#include "PmuXML.h"

// Driver for the gator kernel module
class KMod : public Driver
{
public:
    KMod()
            : Driver("KMod"),
              mIsMaliCapture(false)
    {
    }
    ~KMod()
    {
    }

    bool claimCounter(Counter &counter) const;
    void resetCounters();
    void setupCounter(Counter &counter);

    int writeCounters(mxml_node_t *root) const;

    bool isMaliCapture() const
    {
        return mIsMaliCapture;
    }

    static void checkVersion();
    static std::vector<GatorCpu> writePmuXml(const PmuXML & pmuXml);

private:
    bool mIsMaliCapture;

    static bool isMaliCounter(const Counter &counter);
};

#endif // KMOD_H
