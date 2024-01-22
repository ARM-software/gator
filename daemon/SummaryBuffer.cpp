/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

// Define to adjust Buffer.h interface,
#define BUFFER_USE_SESSION_DATA
// must be before includes

#include "SummaryBuffer.h"

#include "BufferUtils.h"
#include "IBufferControl.h"
#include "IRawFrameBuilder.h"
#include "Protocol.h"
#include "lib/String.h"

#include <cstdint>
#include <map>
#include <string>

#include <semaphore.h>

SummaryBuffer::SummaryBuffer(const int size, sem_t & readerSem) : buffer(size, readerSem)
{
    // fresh buffer will always have room for header
    // so no need to check space
    buffer.beginFrame(FrameType::SUMMARY);
}

void SummaryBuffer::write(ISender & sender)
{
    buffer.write(sender);
}

int SummaryBuffer::bytesAvailable() const
{
    return buffer.bytesAvailable();
}

void SummaryBuffer::flush()
{
    buffer.endFrame();
    buffer.flush();
    buffer.waitForSpace(IRawFrameBuilder::MAX_FRAME_HEADER_SIZE);
    buffer.beginFrame(FrameType::SUMMARY);
}

void SummaryBuffer::summary(const uint64_t timestamp,
                            const uint64_t uptime,
                            const uint64_t monotonicDelta,
                            const char * const uname,
                            const long pageSize,
                            const bool nosync,
                            const std::map<std::string, std::string> & additionalAttributes)
{
    // This is only called when buffer is empty so no need to wait for space
    // we assume the additional attributes won't overflow the buffer??
    buffer.packInt(static_cast<int32_t>(MessageType::SUMMARY));
    buffer.writeString(NEWLINE_CANARY);
    buffer.packInt64(timestamp);
    buffer.packInt64(uptime);
    buffer.packInt64(monotonicDelta);
    buffer.writeString("uname");
    buffer.writeString(uname);
    buffer.writeString("PAGESIZE");
    lib::printf_str_t<32> buf {"%li", pageSize};
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
}

void SummaryBuffer::coreName(const int core, const int cpuid, const char * const name)
{
    waitForSpace(3 * buffer_utils::MAXSIZE_PACK32 + 0x100);
    buffer.packInt(static_cast<int32_t>(MessageType::CORE_NAME));
    buffer.packInt(core);
    buffer.packInt(cpuid);
    buffer.writeString(name);
}

void SummaryBuffer::waitForSpace(int bytes)
{
    if (bytes < buffer.bytesAvailable()) {
        flush();
    }
    buffer.waitForSpace(bytes);
}
