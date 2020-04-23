/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef DRIVER_H
#define DRIVER_H

#include "CapturedSpe.h"
#include "lib/Optional.h"
#include "mxml/mxml.h"

#include <stdint.h>

class Counter;
struct SpeConfiguration;

class Driver {
public:
    // @param name held by reference, not copied
    Driver(const char * name) : name(name) {}
    virtual ~Driver() = default;

    // Returns true if this driver can manage the counter
    virtual bool claimCounter(Counter & counter) const = 0;

    // Clears and disables all counters/SPE
    virtual void resetCounters() = 0;

    // Enables and prepares the counter for capture
    virtual void setupCounter(Counter & counter) = 0;

    // Claims and prepares the SPE for capture
    virtual lib::Optional<CapturedSpe> setupSpe(int /* sampleRate */, const SpeConfiguration & /* configuration */)
    {
        return {};
    }

    // Performs any actions needed for setup or based on eventsXML
    virtual void readEvents(mxml_node_t * const) {}

    // Emits available counters
    // @return number of counters added
    virtual int writeCounters(mxml_node_t * const root) const = 0;

    // Emits possible dynamically generated events/counters
    virtual void writeEvents(mxml_node_t * const) const {}

    inline const char * getName() const { return name; }

    // name pointer is not owned by this so should just be copied
    Driver(const Driver &) = default;
    Driver & operator=(const Driver &) = default;
    Driver(Driver &&) = default;
    Driver & operator=(Driver &&) = default;

private:
    const char * name;
};

#endif // DRIVER_H
