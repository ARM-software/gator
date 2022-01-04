/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef DRIVER_H
#define DRIVER_H

#include "CapturedSpe.h"
#include "Constant.h"
#include "mxml/mxml.h"

#include <cstdint>
#include <optional>
#include <set>

class Counter;
struct SpeConfiguration;

class Driver {
public:
    /// @param name held by reference, not copied
    Driver(const char * name) : name(name) {}
    virtual ~Driver() = default;

    // Returns true if this driver can manage the counter
    virtual bool claimCounter(Counter & counter) const = 0;

    // Clears and disables all counters/SPE
    virtual void resetCounters() = 0;

    // Enables and prepares the counter for capture
    virtual void setupCounter(Counter & counter) = 0;

    // Allow the driver the opportunity to insert a set of
    // constants that it is capable of sending to Streamline
    virtual void insertConstants(std::set<Constant> &) {}

    // Claims and prepares the SPE for capture
    virtual std::optional<CapturedSpe> setupSpe(int /* sampleRate */, const SpeConfiguration & /* configuration */)
    {
        return {};
    }

    // Performs any actions needed for setup or based on eventsXML
    virtual void readEvents(mxml_node_t * const /*unused*/) {}

    // Emits available counters
    // @return number of counters added
    virtual int writeCounters(mxml_node_t * root) const = 0;

    // Emits possible dynamically generated events/counters
    virtual void writeEvents(mxml_node_t * const /*unused*/) const {}

    inline const char * getName() const { return name; }

    /// Called before the gator-child process is forked
    virtual void preChildFork() {}
    /// Called in the parent immediately after the gator-child process is forked
    virtual void postChildForkInParent() {}
    /// Called in the child immediately after the gator-child process is forked
    virtual void postChildForkInChild() {}
    /// Called in the parent after the gator-child process exits
    virtual void postChildExitInParent() {}

    // name pointer is not owned by this so should just be copied
    Driver(const Driver &) = default;
    Driver & operator=(const Driver &) = default;
    Driver(Driver &&) = default;
    Driver & operator=(Driver &&) = default;

private:
    const char * name;
};

#endif // DRIVER_H
