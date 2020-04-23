/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef MIDGARDDRIVER_H
#define MIDGARDDRIVER_H

#include "SimpleDriver.h"

class MidgardDriver : public SimpleDriver {
    typedef SimpleDriver super;

public:
    MidgardDriver();
    ~MidgardDriver();

    bool claimCounter(Counter & counter) const;
    void resetCounters();
    void setupCounter(Counter & counter);

    bool start(const int midgardUds);

private:
    void query() const;

    mutable bool mQueried;

    // Intentionally unimplemented
    MidgardDriver(const MidgardDriver &) = delete;
    MidgardDriver & operator=(const MidgardDriver &) = delete;
    MidgardDriver(MidgardDriver &&) = delete;
    MidgardDriver & operator=(MidgardDriver &&) = delete;
};

#endif // MIDGARDDRIVER_H
