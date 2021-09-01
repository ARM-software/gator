/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef EXTERNALDRIVER_H
#define EXTERNALDRIVER_H

#include "SimpleDriver.h"

class ExternalDriver : public SimpleDriver {
public:
    ExternalDriver();

    // Intentionally unimplemented
    ExternalDriver(const ExternalDriver &) = delete;
    ExternalDriver & operator=(const ExternalDriver &) = delete;
    ExternalDriver(ExternalDriver &&) = delete;
    ExternalDriver & operator=(ExternalDriver &&) = delete;

    bool claimCounter(Counter & counter) const override;
    void resetCounters() override;
    void setupCounter(Counter & counter) override;

    void start();

    void disconnect();

private:
    using super = SimpleDriver;

    bool connect() const;
    void query() const;

    mutable int mUds;
    mutable bool mQueried;
    bool mStarted;
};

#endif // EXTERNALDRIVER_H
