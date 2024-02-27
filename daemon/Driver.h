/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef DRIVER_H
#define DRIVER_H

#include "CapturedSpe.h"
#include "Constant.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <mxml.h>

class Counter;
struct SpeConfiguration;

class Driver {
public:
    enum class counter_type_t {
        counter,
        spe,
    };
    using available_counter_consumer_t = std::function<void(counter_type_t, std::string const &)>;

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
    [[nodiscard]] virtual std::optional<CapturedSpe> setupSpe(int /* sampleRate */,
                                                              const SpeConfiguration & /* configuration */)
    {
        return {};
    }

    // Performs any actions needed for setup or based on eventsXML
    virtual void readEvents(mxml_node_t * const /*unused*/) {}

    // Emits available counters
    // @return number of counters added
    [[nodiscard]] virtual int writeCounters(available_counter_consumer_t const & consumer) const = 0;

    // Emits possible dynamically generated events/counters
    virtual void writeEvents(mxml_node_t * const /*unused*/) const {}

    [[nodiscard]] inline const char * getName() const { return name; }

    /// Called before the gator-child process is forked
    virtual void preChildFork() {}
    /// Called in the parent immediately after the gator-child process is forked
    virtual void postChildForkInParent() {}
    /// Called in the child immediately after the gator-child process is forked
    virtual void postChildForkInChild() {}
    /// Called in the parent after the gator-child process exits
    virtual void postChildExitInParent() {}
    //Any warning messages to be displayed in Streamline post analysis of a capture.
    [[nodiscard]] virtual std::vector<std::string> get_other_warnings() const { return {}; }

    // name pointer is not owned by this so should just be copied
    Driver(const Driver &) = default;
    Driver & operator=(const Driver &) = default;
    Driver(Driver &&) = default;
    Driver & operator=(Driver &&) = default;

private:
    const char * name;
};

#endif // DRIVER_H
