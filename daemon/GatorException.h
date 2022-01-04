/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <stdexcept>
#include <string>

class GatorException : public std::runtime_error {
public:
    GatorException(const std::string & what) : runtime_error(what) {}

    virtual ~GatorException() {}
};
