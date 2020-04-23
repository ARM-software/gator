/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef COUNTER_H
#define COUNTER_H

#include <new>
#include <string.h>

class Driver;

class Counter {
public:
    static const size_t MAX_STRING_LEN = 80;
    static const size_t MAX_DESCRIPTION_LEN = 400;

    Counter() : mType(), mEnabled(false), mEvent(-1), mCount(0), mCores(-1), mKey(0), mDriver(nullptr)
    {
        mType[0] = '\0';
    }

    void clear()
    {
        // use placement new to call the constructor again
        new (static_cast<void *>(this)) Counter();
    }

    void setType(const char * const type)
    {
        strncpy(mType, type, sizeof(mType));
        mType[sizeof(mType) - 1] = '\0';
    }
    void setEnabled(const bool enabled) { mEnabled = enabled; }
    void setEvent(const int event) { mEvent = event; }
    void setCount(const int count) { mCount = count; }
    void setCores(const int cores) { mCores = cores; }
    void setKey(const int key) { mKey = key; }
    void setDriver(Driver * const driver) { mDriver = driver; }

    const char * getType() const { return mType; }
    bool isEnabled() const { return mEnabled; }
    int getEvent() const { return mEvent; }
    int getCount() const { return mCount; }
    int getCores() const { return mCores; }
    int getKey() const { return mKey; }
    Driver * getDriver() const { return mDriver; }

private:
    // Intentionally unimplemented
    Counter(const Counter &) = delete;
    Counter & operator=(const Counter &) = delete;
    Counter(Counter &&) = delete;
    Counter & operator=(Counter &&) = delete;

    char mType[MAX_STRING_LEN];
    bool mEnabled;
    int mEvent;
    int mCount;
    int mCores;
    int mKey;
    Driver * mDriver;
};

#endif // COUNTER_H
