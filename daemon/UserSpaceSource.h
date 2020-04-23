/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef USERSPACESOURCE_H
#define USERSPACESOURCE_H

#include "Buffer.h"
#include "Source.h"
#include "lib/Span.h"

#include <functional>
#include <semaphore.h>

// Forward decl for allPolledDrivers
class PolledDriver;
class PrimarySourceProvider;

// User space counters
class UserSpaceSource : public Source {
public:
    UserSpaceSource(Child & child,
                    sem_t * senderSem,
                    std::function<std::int64_t()> mGetMonotonicStarted,
                    lib::Span<PolledDriver * const> drivers);
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
    UserSpaceSource(const UserSpaceSource &) = delete;
    UserSpaceSource & operator=(const UserSpaceSource &) = delete;
    UserSpaceSource(UserSpaceSource &&) = delete;
    UserSpaceSource & operator=(UserSpaceSource &&) = delete;
};

#endif // USERSPACESOURCE_H
