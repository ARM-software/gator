/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef COUNTER_H
#define COUNTER_H

#include "EventCode.h"

#include <new>
#include <string>

class Driver;

class Counter {
public:
    static const size_t MAX_DESCRIPTION_LEN = 400;

    Counter() = default;

    // Intentionally unimplemented
    Counter(const Counter &) = delete;
    Counter & operator=(const Counter &) = delete;
    Counter(Counter &&) = delete;
    Counter & operator=(Counter &&) = delete;

    void clear()
    {
        // use placement new to call the constructor again
        new (static_cast<void *>(this)) Counter();
    }

    void setType(const char * const type) { mType = type; }
    void setEnabled(const bool enabled) { mEnabled = enabled; }
    void setEventCode(const EventCode event) { mEvent = event; }
    void setCount(const int count) { mCount = count; }
    void setCores(const int cores) { mCores = cores; }
    void setKey(const int key) { mKey = key; }
    void setDriver(Driver * const driver) { mDriver = driver; }

    const char * getType() const { return mType.c_str(); }
    bool isEnabled() const { return mEnabled; }
    EventCode getEventCode() const { return mEvent; }
    int getCount() const { return mCount; }
    int getCores() const { return mCores; }
    int getKey() const { return mKey; }
    Driver * getDriver() const { return mDriver; }
    void setExcludeFromCapturedXml() { mExcludeFromCapturedXml = true; }
    bool excludeFromCapturedXml() const { return mExcludeFromCapturedXml; }

private:
    std::string mType {};
    bool mEnabled {false};
    EventCode mEvent {};
    int mCount {0};
    int mCores {-1};
    int mKey {0};
    Driver * mDriver {nullptr};
    bool mExcludeFromCapturedXml = false;
};

#endif // COUNTER_H
