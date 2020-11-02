/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef SUMMARY_BUFFER_H
#define SUMMARY_BUFFER_H

#include "Buffer.h"
#include "ISummaryConsumer.h"

class SummaryBuffer : public ISummaryConsumer {
public:
    SummaryBuffer(int size, sem_t & readerSem);

    void write(ISender & sender);

    int bytesAvailable() const;
    virtual void flush() override;

    // Summary messages
    virtual void summary(int64_t timestamp,
                         int64_t uptime,
                         int64_t monotonicDelta,
                         const char * uname,
                         long pageSize,
                         bool nosync,
                         const std::map<std::string, std::string> & additionalAttributes) override;
    virtual void coreName(int core, int cpuid, const char * name) override;

private:
    void waitForSpace(int bytes);
    Buffer buffer;
};

#endif // SUMMARY_BUFFER_H
