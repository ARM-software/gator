/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXTERNALDRIVER_H
#define EXTERNALDRIVER_H

#include "ClassBoilerPlate.h"
#include "SimpleDriver.h"

class ExternalDriver : public SimpleDriver
{
public:
    ExternalDriver();

    bool claimCounter(Counter &counter) const;
    void resetCounters();
    void setupCounter(Counter &counter);

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
    CLASS_DELETE_COPY_MOVE(ExternalDriver);
};

#endif // EXTERNALDRIVER_H
