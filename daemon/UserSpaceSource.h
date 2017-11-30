/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef USERSPACESOURCE_H
#define USERSPACESOURCE_H

#include <semaphore.h>

#include "ClassBoilerPlate.h"
#include "Buffer.h"
#include "Source.h"

#include <vector>

// Forward decl for allPolledDrivers
class PolledDriver;

// User space counters
class UserSpaceSource : public Source
{
public:
    UserSpaceSource(Child & child, sem_t *senderSem);
    ~UserSpaceSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(Sender * sender) override;

    static bool shouldStart();
    static std::vector<PolledDriver *> allPolledDrivers();

private:
    Buffer mBuffer;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(UserSpaceSource);
};

#endif // USERSPACESOURCE_H
