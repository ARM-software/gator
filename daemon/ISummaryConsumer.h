/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef I_SUMMARY_CONSUMER_H
#define I_SUMMARY_CONSUMER_H

#include <cstdint>
#include <map>
#include <string>

class ISummaryConsumer {
public:
    virtual void flush() = 0;

    // Summary messages
    virtual void summary(uint64_t timestamp,
                         uint64_t uptime,
                         uint64_t monotonicDelta,
                         const char * uname,
                         long pageSize,
                         bool nosync,
                         const std::map<std::string, std::string> & additionalAttributes) = 0;

    virtual void coreName(int core, int cpuid, const char * name) = 0;

    virtual ~ISummaryConsumer() = default;
};

#endif
