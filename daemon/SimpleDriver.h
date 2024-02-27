/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_
#define NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_

#include "Driver.h"
#include "DriverCounter.h"

class SimpleDriver : public Driver {
public:
    ~SimpleDriver() override;

    // Intentionally unimplemented
    SimpleDriver(const SimpleDriver &) = delete;
    SimpleDriver & operator=(const SimpleDriver &) = delete;
    SimpleDriver(SimpleDriver &&) = delete;
    SimpleDriver & operator=(SimpleDriver &&) = delete;

    [[nodiscard]] bool claimCounter(Counter & counter) const override;
    [[nodiscard]] bool countersEnabled() const;
    void resetCounters() override;
    void setupCounter(Counter & counter) override;
    [[nodiscard]] int writeCounters(available_counter_consumer_t const & consumer) const override;

protected:
    SimpleDriver(const char * name) : Driver(name), mCounters(nullptr) {}

    [[nodiscard]] DriverCounter * getCounters() const { return mCounters; }

    void setCounters(DriverCounter * const counter) { mCounters = counter; }

    [[nodiscard]] DriverCounter * findCounter(Counter & counter) const;

private:
    DriverCounter * mCounters;
};

#endif /* NATIVE_GATOR_DAEMON_SIMPLEDRIVER_H_ */
