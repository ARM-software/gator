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
    virtual void commit(uint64_t time) override;

    // Summary messages
    virtual void summary(uint64_t currTime,
                         int64_t timestamp,
                         int64_t uptime,
                         int64_t monotonicDelta,
                         const char * uname,
                         long pageSize,
                         bool nosync,
                         const std::map<std::string, std::string> & additionalAttributes) override;
    virtual void coreName(uint64_t currTime, int core, int cpuid, const char * name) override;

    void setDone();
    bool isDone() const;

private:
    Buffer buffer;
};

#endif // SUMMARY_BUFFER_H
