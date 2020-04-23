/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef EXTERNALDRIVER_H
#define EXTERNALDRIVER_H

#include "SimpleDriver.h"

class ExternalDriver : public SimpleDriver {
public:
    ExternalDriver();

    bool claimCounter(Counter & counter) const;
    void resetCounters();
    void setupCounter(Counter & counter);

    void start();

    void disconnect();

private:
    typedef SimpleDriver super;

    bool connect() const;
    void query() const;

    mutable int mUds;
    mutable bool mQueried;
    bool mStarted;

    // Intentionally unimplemented
    ExternalDriver(const ExternalDriver &) = delete;
    ExternalDriver & operator=(const ExternalDriver &) = delete;
    ExternalDriver(ExternalDriver &&) = delete;
    ExternalDriver & operator=(ExternalDriver &&) = delete;
};

#endif // EXTERNALDRIVER_H
