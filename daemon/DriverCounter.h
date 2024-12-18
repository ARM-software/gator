/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_
#define NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_

#include "metrics/metric_group_set.hpp"

#include <string>

class DriverCounter {
public:
    /**
     * @param name will be copied
     */
    DriverCounter(DriverCounter * next, const char * name);
    virtual ~DriverCounter() = default;

    // Intentionally unimplemented
    DriverCounter(const DriverCounter &) = delete;
    DriverCounter & operator=(const DriverCounter &) = delete;
    DriverCounter(DriverCounter &&) = delete;
    DriverCounter & operator=(DriverCounter &&) = delete;

    DriverCounter * getNext() const { return mNext; }
    [[nodiscard]] const char * getName() const { return mName.c_str(); }
    [[nodiscard]] int getKey() const { return mKey; }
    [[nodiscard]] bool isEnabled() const { return mEnabled; }
    void setEnabled(const bool enabled) { mEnabled = enabled; }
    virtual int64_t read() { return -1; }
    [[nodiscard]] virtual bool supportsAtLeastOne(metrics::metric_group_set_t const & /*unused*/) const
    {
        return false;
    }

private:
    DriverCounter * const mNext;
    const std::string mName;
    const int mKey;
    bool mEnabled;
};

#endif /* NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_ */
