/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef I_SUMMARY_CONSUMER_H
#define I_SUMMARY_CONSUMER_H

#include <cstdint>
#include <map>
#include <string>

class ISummaryConsumer {
public:
    virtual void commit(uint64_t time) = 0;

    // Summary messages
    virtual void summary(uint64_t currTime,
                         int64_t timestamp,
                         int64_t uptime,
                         int64_t monotonicDelta,
                         const char * uname,
                         long pageSize,
                         bool nosync,
                         const std::map<std::string, std::string> & additionalAttributes) = 0;
    virtual void coreName(uint64_t currTime, int core, int cpuid, const char * name) = 0;

    virtual ~ISummaryConsumer() = default;
};

#endif
