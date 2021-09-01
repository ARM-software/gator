/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_
#define NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_

#include "Driver.h"

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
    const char * getName() const { return mName.c_str(); }
    int getKey() const { return mKey; }
    bool isEnabled() const { return mEnabled; }
    void setEnabled(const bool enabled) { mEnabled = enabled; }
    virtual int64_t read() { return -1; }

private:
    DriverCounter * const mNext;
    const std::string mName;
    const int mKey;
    bool mEnabled;
};

#endif /* NATIVE_GATOR_DAEMON_DRIVERCOUNTER_H_ */
