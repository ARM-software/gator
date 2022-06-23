/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#ifndef SUMMARY_BUFFER_H
#define SUMMARY_BUFFER_H

#include "Buffer.h"
#include "ISummaryConsumer.h"

class SummaryBuffer : public ISummaryConsumer {
public:
    SummaryBuffer(int size, sem_t & readerSem);

    void write(ISender & sender);

    int bytesAvailable() const;
    void flush() override;

    // Summary messages
    void summary(uint64_t timestamp,
                 uint64_t uptime,
                 uint64_t monotonicDelta,
                 const char * uname,
                 long pageSize,
                 bool nosync,
                 const std::map<std::string, std::string> & additionalAttributes) override;
    void coreName(int core, int cpuid, const char * name) override;

private:
    void waitForSpace(int bytes);
    Buffer buffer;
};

#endif // SUMMARY_BUFFER_H
