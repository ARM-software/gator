/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

#include "mxml/mxml.h"

#include "lib/Optional.h"
#include "CapturedSpe.h"
#include "ClassBoilerPlate.h"

class Counter;
struct SpeConfiguration;

class Driver
{
public:
    Driver(const char * name) : name(name){}
    virtual ~Driver() = default;

    // Returns true if this driver can manage the counter
    virtual bool claimCounter(Counter &counter) const = 0;

    // Clears and disables all counters/SPE
    virtual void resetCounters() = 0;

    // Enables and prepares the counter for capture
    virtual void setupCounter(Counter &counter) = 0;

    // Claims and prepares the SPE for capture
    virtual lib::Optional<CapturedSpe> setupSpe(const SpeConfiguration &)
    {
        return {};
    }

    // Performs any actions needed for setup or based on eventsXML
    virtual void readEvents(mxml_node_t * const)
    {
    }

    // Emits available counters
    virtual int writeCounters(mxml_node_t * const root) const = 0;

    // Emits possible dynamically generated events/counters
    virtual void writeEvents(mxml_node_t * const) const
    {
    }

    inline const char * getName() const
    {
        return name;
    }

    CLASS_DEFAULT_COPY_MOVE(Driver);

private:

    const char * name;

};

#endif // DRIVER_H
