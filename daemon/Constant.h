/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#ifndef CONSTANT_H
#define CONSTANT_H

#include "ConstantMode.h"
#include "GetEventKey.h"
#include "lib/Optional.h"

#include <string>

/**
 * Represents something that a Driver can send at the start of a capture.
 * An instance of this class is intended to be associated with a separate value
 * that shouldn't change for the lifetime of a capture.
 *
 * The key identifies the meaning of the constant's associated value
 * when it is transmitted down to Streamline.
 */
class Constant {
public:
    Constant(CounterKey key, std::string counterString, std::string title, std::string name, ConstantMode mode);

    CounterKey getKey() const { return mKey; }

    const std::string & getCounterString() const { return mCounterString; }

    const std::string & getTitle() const { return mTitle; }

    const std::string & getName() const { return mName; }

    ConstantMode getMode() const { return mMode; }

    bool operator<(const Constant & rhs) const;

private:
    CounterKey mKey;
    std::string mCounterString;
    std::string mTitle;
    std::string mName;
    ConstantMode mMode;
};

#endif // CONSTANT_H
