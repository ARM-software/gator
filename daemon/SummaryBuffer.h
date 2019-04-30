/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SUMMARY_BUFFER_H
#define SUMMARY_BUFFER_H

#include "ISummaryConsumer.h"

#include "Buffer.h"

class SummaryBuffer : public ISummaryConsumer
{
public:
    SummaryBuffer(int size, sem_t * const readerSem);

    void write(ISender *sender);

    int bytesAvailable() const;
    virtual void commit(const uint64_t time) override;

    // Summary messages
    virtual void summary(const uint64_t currTime, const int64_t timestamp, const int64_t uptime, const int64_t monotonicDelta,
                 const char * const uname, const long pageSize, const bool nosync,
                 const std::map<std::string, std::string> & additionalAttributes) override;
    virtual void coreName(const uint64_t currTime, const int core, const int cpuid, const char * const name) override;


    void setDone();
    bool isDone() const;

private:

    Buffer buffer;
};

#endif // SUMMARY_BUFFER_H
