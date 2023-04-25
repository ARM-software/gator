/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

#include "non_root/MixedFrameBuffer.h"

#include "BufferUtils.h"
#include "IRawFrameBuilder.h"
#include "Logging.h"
#include "Sender.h"
#include "lib/String.h"

#include <cstring>
#include <string_view>

namespace non_root {
    MixedFrameBuffer::Frame::Frame(IRawFrameBuilder & parent_, FrameType frameType)
        : parent(parent_),
          bytesAvailable(parent.bytesAvailable() - IRawFrameBuilder::MAX_FRAME_HEADER_SIZE),
          valid(false)
    {
        if (bytesAvailable >= 0) {
            parent.beginFrame(frameType);
            valid = true;
        }
    }

    MixedFrameBuffer::Frame::~Frame()
    {
        if (valid) {
            parent.endFrame();
        }
        else {
            parent.abortFrame();
        }
    }

    bool MixedFrameBuffer::Frame::checkSize(int size)
    {
        if (valid && (bytesAvailable >= size)) {
            bytesAvailable -= size;
        }
        else {
            LOG_ERROR("checkSize: %i failed (%u)", size, valid);

            valid = false;
        }

        return valid;
    }

    void MixedFrameBuffer::Frame::packInt(std::int32_t value)
    {
        // determine the length by writing it to some temp buffer
        const int size = buffer_utils::sizeOfPackInt(value);

        if (checkSize(size)) {
            parent.packInt(value);
        }
    }

    void MixedFrameBuffer::Frame::packInt64(std::int64_t value)
    {
        // determine the length by writing it to some temp buffer
        const int size = buffer_utils::sizeOfPackInt64(value);

        if (checkSize(size)) {
            parent.packInt64(value);
        }
    }

    void MixedFrameBuffer::Frame::writeString(std::string_view value)
    {
        auto len = value.size();
        if (len > std::numeric_limits<int>::max()) {
            len = std::numeric_limits<int>::max();
        }

        auto ilen = static_cast<int32_t>(len);

        const int size = buffer_utils::sizeOfPackInt(ilen) + ilen;

        if (checkSize(size)) {
            parent.writeString(value);
        }
    }

    bool MixedFrameBuffer::Frame::isValid() const
    {
        return valid;
    }

    MixedFrameBuffer::MixedFrameBuffer(IRawFrameBuilder & buffer_, CommitTimeChecker flushIsNeeded_)
        : buffer(buffer_), flushIsNeeded(flushIsNeeded_)
    {
    }

    bool MixedFrameBuffer::activityFrameLinkMessage(std::uint64_t currentTime,
                                                    std::int32_t cookie,
                                                    std::int32_t pid,
                                                    std::int32_t tid)
    {
        Frame frame(buffer, FrameType::ACTIVITY_TRACE);

        frame.packInt(static_cast<int32_t>(MessageType::LINK));
        frame.packInt64(currentTime);
        frame.packInt(cookie);
        frame.packInt(pid);
        frame.packInt(tid);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::counterFrameMessage(std::uint64_t currentTime,
                                               std::int32_t core,
                                               std::int32_t key,
                                               std::uint64_t value)
    {
        Frame frame(buffer, FrameType::COUNTER);

        frame.packInt64(currentTime);
        frame.packInt(core);
        frame.packInt(key);
        frame.packInt64(value);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::nameFrameCookieNameMessage(std::uint64_t currentTime,
                                                      std::int32_t core,
                                                      std::int32_t cookie,
                                                      const std::string & name)
    {
        Frame frame(buffer, FrameType::NAME);
        frame.packInt(core);

        frame.packInt(static_cast<int32_t>(MessageType::COOKIE_NAME));
        frame.packInt(cookie);
        frame.writeString(name);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::nameFrameThreadNameMessage(std::uint64_t currentTime,
                                                      std::int32_t core,
                                                      std::int32_t tid,
                                                      const std::string & name)
    {
        Frame frame(buffer, FrameType::NAME);
        frame.packInt(core);

        frame.packInt(static_cast<int32_t>(MessageType::THREAD_NAME));
        frame.packInt64(currentTime);
        frame.packInt(tid);
        frame.writeString(name);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::schedFrameSwitchMessage(std::uint64_t currentTime,
                                                   std::int32_t core,
                                                   std::int32_t tid,
                                                   std::int32_t state)
    {
        Frame frame(buffer, FrameType::SCHED_TRACE);
        frame.packInt(core);

        frame.packInt(static_cast<int32_t>(MessageType::SCHED_SWITCH));
        frame.packInt64(currentTime);
        frame.packInt(tid);
        frame.packInt(state);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::schedFrameThreadExitMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid)
    {
        Frame frame(buffer, FrameType::SCHED_TRACE);
        frame.packInt(core);

        frame.packInt(static_cast<int32_t>(MessageType::THREAD_EXIT));
        frame.packInt64(currentTime);
        frame.packInt(tid);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::summaryFrameSummaryMessage(std::uint64_t currentTime,
                                                      std::uint64_t timestamp,
                                                      std::uint64_t uptime,
                                                      std::uint64_t monotonicDelta,
                                                      const char * uname,
                                                      unsigned long pageSize,
                                                      bool nosync)
    {
        MixedFrameBuffer::Frame frame(buffer, FrameType::SUMMARY);

        frame.packInt(static_cast<int32_t>(MessageType::SUMMARY));
        frame.writeString(NEWLINE_CANARY);
        frame.packInt64(timestamp);
        frame.packInt64(uptime);
        frame.packInt64(monotonicDelta);
        frame.writeString("uname");
        frame.writeString(uname);
        frame.writeString("PAGESIZE");
        lib::printf_str_t<32> buf {"%li", pageSize};
        frame.writeString(buf);
        if (nosync) {
            frame.writeString("nosync");
            frame.writeString("");
        }
        frame.writeString("");

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::summaryFrameCoreNameMessage(std::uint64_t currentTime,
                                                       std::int32_t core,
                                                       std::int32_t cpuid,
                                                       const char * name)
    {
        MixedFrameBuffer::Frame frame(buffer, FrameType::SUMMARY);

        frame.packInt(static_cast<int32_t>(MessageType::CORE_NAME));
        frame.packInt(core);
        frame.packInt(cpuid);
        frame.writeString(name);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    bool MixedFrameBuffer::threadCounterFrameMessage(std::uint64_t currentTime,
                                                     std::int32_t core,
                                                     std::int32_t tid,
                                                     std::int32_t key,
                                                     std::uint64_t value)
    {
        // have to send as block counter in order to be able to send tid :-(
        Frame frame(buffer, FrameType::BLOCK_COUNTER);

        frame.packInt(core);
        frame.packInt(0);
        frame.packInt64(currentTime);
        frame.packInt(1);
        frame.packInt64(tid);
        frame.packInt(key);
        frame.packInt64(value);

        flushIfNeeded(currentTime);

        return frame.isValid();
    }

    void MixedFrameBuffer::flushIfNeeded(std::uint64_t currentTime)
    {
        if (flushIsNeeded(currentTime, buffer.needsFlush())) {
            buffer.flush();
        }
    }
}
