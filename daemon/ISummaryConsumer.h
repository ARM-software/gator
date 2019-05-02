/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef I_SUMMARY_CONSUMER_H
#define I_SUMMARY_CONSUMER_H

#include <cstdint>
#include <map>
#include <string>

class ISummaryConsumer
{
public:

    virtual void commit(const uint64_t time)  = 0;

    // Summary messages
    virtual void summary(const uint64_t currTime, const int64_t timestamp, const int64_t uptime, const int64_t monotonicDelta,
                 const char * const uname, const long pageSize, const bool nosync,
                 const std::map<std::string, std::string> & additionalAttributes)  = 0;
    virtual void coreName(const uint64_t currTime, const int core, const int cpuid, const char * const name)  = 0;

    virtual ~ISummaryConsumer() = default;


};

#endif
