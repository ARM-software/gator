/* Copyright (C) 2017-2022 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H
#define INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H

#include "CommitTimeChecker.h"
#include "Protocol.h"

#include <cstdint>
#include <string>
#include <string_view>

class IRawFrameBuilder;
class Sender;

namespace non_root {

    class MixedFrameBuffer {
    public:
        class Frame {
        public:
            Frame(IRawFrameBuilder & parent, FrameType frameType);
            ~Frame();

            void packInt(std::int32_t);
            void packInt64(std::int64_t);
            void writeString(std::string_view);
            bool isValid() const;

        private:
            IRawFrameBuilder & parent;
            int bytesAvailable;
            bool valid;

            bool checkSize(int size);
        };

        using size_type = unsigned long;
        using size_diff_type = long;

        MixedFrameBuffer(IRawFrameBuilder & buffer, CommitTimeChecker flushIsNeeded);

        // Intentionally unimplemented
        MixedFrameBuffer(const MixedFrameBuffer &) = delete;
        MixedFrameBuffer & operator=(const MixedFrameBuffer &) = delete;
        MixedFrameBuffer(MixedFrameBuffer &&) = delete;
        MixedFrameBuffer & operator=(MixedFrameBuffer &&) = delete;

        bool activityFrameLinkMessage(std::uint64_t currentTime,
                                      std::int32_t cookie,
                                      std::int32_t pid,
                                      std::int32_t tid);
        bool counterFrameMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t key, std::uint64_t value);
        bool nameFrameCookieNameMessage(std::uint64_t currentTime,
                                        std::int32_t core,
                                        std::int32_t cookie,
                                        const std::string & name);
        bool nameFrameThreadNameMessage(std::uint64_t currentTime,
                                        std::int32_t core,
                                        std::int32_t tid,
                                        const std::string & name);
        bool schedFrameSwitchMessage(std::uint64_t currentTime,
                                     std::int32_t core,
                                     std::int32_t tid,
                                     std::int32_t state);
        bool schedFrameThreadExitMessage(std::uint64_t currentTime, std::int32_t core, std::int32_t tid);
        bool summaryFrameSummaryMessage(std::uint64_t currentTime,
                                        std::uint64_t timestamp,
                                        std::uint64_t uptime,
                                        std::uint64_t monotonicDelta,
                                        const char * uname,
                                        unsigned long pageSize,
                                        bool nosync);
        bool summaryFrameCoreNameMessage(std::uint64_t currentTime,
                                         std::int32_t core,
                                         std::int32_t cpuid,
                                         const char * name);
        bool threadCounterFrameMessage(std::uint64_t currentTime,
                                       std::int32_t core,
                                       std::int32_t tid,
                                       std::int32_t key,
                                       std::uint64_t value);

    private:
        void flushIfNeeded(std::uint64_t currentTime);

        IRawFrameBuilder & buffer;
        CommitTimeChecker flushIsNeeded;
    };
}

#endif /* INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H */
