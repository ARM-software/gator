/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

#include "Constant.h"

#include "ConstantMode.h"
#include "GetEventKey.h"

#include <string>
#include <utility>

Constant::Constant(CounterKey key, std::string counterString, std::string title, std::string name, ConstantMode mode)
    : mKey(key), mCounterString(std::move(counterString)), mTitle(std::move(title)), mName(std::move(name)), mMode(mode)
{
}

bool Constant::operator<(const Constant & rhs) const
{
    return mCounterString < rhs.mCounterString;
}
