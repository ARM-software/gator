/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <cstring>

#include "SummaryBuffer.h"
#include "BufferUtils.h"
#include "Logging.h"

SummaryBuffer::SummaryBuffer(const int size, sem_t * const readerSem)
        : buffer(0 /* ignored */, FrameType::SUMMARY, size, readerSem)
{
}

void SummaryBuffer::write(ISender * const sender)
{
    buffer.write(sender);
}

int SummaryBuffer::bytesAvailable() const
{
    return buffer.bytesAvailable();
}

void SummaryBuffer::commit(const uint64_t time)
{
    buffer.commit(time);
}

void SummaryBuffer::setDone()
{
    buffer.setDone();
}

bool SummaryBuffer::isDone() const
{
    return buffer.isDone();
}

void SummaryBuffer::summary(const uint64_t currTime, const int64_t timestamp, const int64_t uptime,
                            const int64_t monotonicDelta, const char * const uname, const long pageSize,
                            const bool nosync, const std::map<std::string, std::string> & additionalAttributes)
{
    buffer.packInt(static_cast<int32_t>(MessageType::SUMMARY));
    buffer.writeString(NEWLINE_CANARY);
    buffer.packInt64(timestamp);
    buffer.packInt64(uptime);
    buffer.packInt64(monotonicDelta);
    buffer.writeString("uname");
    buffer.writeString(uname);
    buffer.writeString("PAGESIZE");
    char buf[32];
    snprintf(buf, sizeof(buf), "%li", pageSize);
    buffer.writeString(buf);
    if (nosync) {
        buffer.writeString("nosync");
        buffer.writeString("");
    }
    for (const auto & pair : additionalAttributes) {
        if (!pair.first.empty()) {
            buffer.writeString(pair.first.c_str());
            buffer.writeString(pair.second.c_str());
        }
    }
    buffer.writeString("");
    buffer.check(currTime);
}

void SummaryBuffer::coreName(const uint64_t currTime, const int core, const int cpuid, const char * const name)
{
    buffer.waitForSpace(3 * buffer_utils::MAXSIZE_PACK32 + 0x100);
    buffer.packInt(static_cast<int32_t>(MessageType::CORE_NAME));
    buffer.packInt(core);
    buffer.packInt(cpuid);
    buffer.writeString(name);
    buffer.check(currTime);
}

