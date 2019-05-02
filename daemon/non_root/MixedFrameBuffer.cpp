/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/MixedFrameBuffer.h"
#include "Buffer.h"
#include "BufferUtils.h"
#include "Sender.h"
#include "Logging.h"

#include <cstring>

namespace non_root
{
    MixedFrameBuffer::Frame::Frame(MixedFrameBuffer & parent_, std::uint64_t currentTime_, FrameType frameType,
                                   std::int32_t core)
            : parent(parent_),
              currentTime(currentTime_),
              bytesAvailable(parent.buffer.bytesAvailable() - buffer_utils::MAX_FRAME_HEADER_SIZE),
              frameStart(-1),
              valid(false)
    {
        if (bytesAvailable >= 0) {
            frameStart = parent.buffer.beginFrameOrMessage(frameType, core);
            valid = true;
        }
    }

    MixedFrameBuffer::Frame::~Frame()
    {
        if (frameStart >= 0) {
            parent.buffer.endFrame(currentTime, !valid, frameStart);
        }
    }

    bool MixedFrameBuffer::Frame::checkSize(int size)
    {
        if (valid && (bytesAvailable >= size)) {
            bytesAvailable -= size;
        }
        else {
            logg.logError("checkSize: %i failed (%u)", size, valid);

            valid = false;
        }

        return valid;
    }

    void MixedFrameBuffer::Frame::packInt(std::int32_t value)
    {
        // determine the length by writing it to some temp buffer
        const int size = buffer_utils::sizeOfPackInt(value);

        if (checkSize(size)) {
            parent.buffer.packInt(value);
        }
    }

    void MixedFrameBuffer::Frame::packInt64(std::int64_t value)
    {
        // determine the length by writing it to some temp buffer
        const int size = buffer_utils::sizeOfPackInt64(value);

        if (checkSize(size)) {
            parent.buffer.packInt64(value);
        }
    }

    void MixedFrameBuffer::Frame::writeString(const char * value)
    {
        const int length = std::strlen(value);
        const int size = buffer_utils::sizeOfPackInt(length) + length;

        if (checkSize(size)) {
            parent.buffer.writeString(value);
        }
    }

    void MixedFrameBuffer::Frame::writeString(const std::string & value)
    {
        const int length = value.length();
        const int size = buffer_utils::sizeOfPackInt(length) + length;

        if (checkSize(size)) {
            parent.buffer.packInt(length);
            parent.buffer.writeBytes(value.data(), length);
        }
    }

    bool MixedFrameBuffer::Frame::isValid() const
    {
        return valid;
    }

    MixedFrameBuffer::MixedFrameBuffer(Buffer & buffer_)
            : buffer(buffer_)
    {
    }

    bool MixedFrameBuffer::activityFrameLinkMessage(std::uint64_t currentTime, std::int32_t cookie, std::int32_t pid,
                                                    std::int32_t tid)
    {
        Frame frame(*this, currentTime, FrameType::ACTIVITY_TRACE, 0);

        frame.packInt(static_cast<int32_t>(MessageType::LINK));
        frame.packInt64(currentTime);
        frame.packInt(cookie);
        frame.packInt(pid);
        frame.packInt(tid);

        return frame.isValid();
    }

    bool MixedFrameBuffer::counterFrameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t key,
                                               std::uint64_t value)
    {
        Frame frame(*this, currentTime, FrameType::COUNTER, core);

        frame.packInt64(currentTime);
        frame.packInt(core);
        frame.packInt(key);
        frame.packInt64(value);

        return frame.isValid();
    }

    bool MixedFrameBuffer::nameFrameCookieNameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t cookie,
                                                      const std::string & name)
    {
        Frame frame(*this, currentTime, FrameType::NAME, core);

        frame.packInt(static_cast<int32_t>(MessageType::COOKIE_NAME));
        frame.packInt(cookie);
        frame.writeString(name);

        return frame.isValid();
    }

    bool MixedFrameBuffer::nameFrameThreadNameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid,
                                                      const std::string & name)
    {
        Frame frame(*this, currentTime, FrameType::NAME, core);

        frame.packInt(static_cast<int32_t>(MessageType::THREAD_NAME));
        frame.packInt64(currentTime);
        frame.packInt(tid);
        frame.writeString(name);

        return frame.isValid();
    }

    bool MixedFrameBuffer::schedFrameSwitchMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid,
                                                   std::int32_t state)
    {
        Frame frame(*this, currentTime, FrameType::SCHED_TRACE, core);

        frame.packInt(static_cast<int32_t>(MessageType::SCHED_SWITCH));
        frame.packInt64(currentTime);
        frame.packInt(tid);
        frame.packInt(state);

        return frame.isValid();
    }

    bool MixedFrameBuffer::schedFrameThreadExitMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid)
    {
        Frame frame(*this, currentTime, FrameType::SCHED_TRACE, core);

        frame.packInt(static_cast<int32_t>(MessageType::THREAD_EXIT));
        frame.packInt64(currentTime);
        frame.packInt(tid);

        return frame.isValid();
    }

    bool MixedFrameBuffer::summaryFrameSummaryMessage(std::uint64_t currentTime, std::uint64_t timestamp, std::uint64_t uptime, std::uint64_t monotonicDelta,
                                    const char * uname, unsigned long pageSize, bool nosync)
    {
        MixedFrameBuffer::Frame frame (*this, currentTime, FrameType::SUMMARY, 0);

        frame.packInt(static_cast<int32_t>(MessageType::SUMMARY));
        frame.writeString(NEWLINE_CANARY);
        frame.packInt64(timestamp);
        frame.packInt64(uptime);
        frame.packInt64(monotonicDelta);
        frame.writeString("uname");
        frame.writeString(uname);
        frame.writeString("PAGESIZE");
        char buf[32];
        snprintf(buf, sizeof(buf), "%li", pageSize);
        frame.writeString(buf);
        if (nosync) {
            frame.writeString("nosync");
            frame.writeString("");
        }
        frame.writeString("");

        return frame.isValid();
    }

    bool MixedFrameBuffer::summaryFrameCoreNameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t cpuid, const char * name)
    {
        MixedFrameBuffer::Frame frame (*this, currentTime, FrameType::SUMMARY, 0);

        frame.packInt(static_cast<int32_t>(MessageType::CORE_NAME));
        frame.packInt(core);
        frame.packInt(cpuid);
        frame.writeString(name);

        return frame.isValid();
    }

    bool MixedFrameBuffer::threadCounterFrameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid,
                                                     std::int32_t key, std::uint64_t value)
    {
        // have to send as block counter in order to be able to send tid :-(
        Frame frame(*this, currentTime, FrameType::BLOCK_COUNTER, core);

        frame.packInt(0);
        frame.packInt64(currentTime);
        frame.packInt(1);
        frame.packInt64(tid);
        frame.packInt(key);
        frame.packInt64(value);

        return frame.isValid();
    }
}
