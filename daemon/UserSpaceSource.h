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
#include "lib/Span.h"

#include <functional>

// Forward decl for allPolledDrivers
class PolledDriver;
class PrimarySourceProvider;

// User space counters
class UserSpaceSource : public Source
{
public:
    UserSpaceSource(Child & child, sem_t *senderSem, std::function<std::int64_t()> mGetMonotonicStarted, lib::Span<PolledDriver * const> drivers);
    ~UserSpaceSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender * sender) override;

    static bool shouldStart(lib::Span<const PolledDriver * const>);

private:
    Buffer mBuffer;
    std::function<std::int64_t()> mGetMonotonicStarted;
    lib::Span<PolledDriver * const> mDrivers;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(UserSpaceSource);
};

#endif // USERSPACESOURCE_H
