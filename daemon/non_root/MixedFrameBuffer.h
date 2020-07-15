/* Copyright (C) 2017-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H
#define INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H

#include "Protocol.h"

#include <cstdint>
#include <string>

class Buffer;
class Sender;

namespace non_root {
    class MixedFrameBuffer {
    public:
        class Frame {
        public:
            Frame(MixedFrameBuffer & parent, std::uint64_t currentTime, FrameType frameType, std::int32_t core);
            ~Frame();

            void packInt(std::int32_t);
            void packInt64(std::int64_t);
            void writeString(const char *);
            void writeString(const std::string &);
            bool isValid() const;

        private:
            MixedFrameBuffer & parent;
            std::uint64_t currentTime;
            int bytesAvailable;
            int frameStart;
            bool valid;

            bool checkSize(int size);
        };

        using size_type = unsigned long;
        using size_diff_type = long;

        MixedFrameBuffer(Buffer & buffer);

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
        friend class Frame;

        Buffer & buffer;

        // Intentionally unimplemented
        MixedFrameBuffer(const MixedFrameBuffer &) = delete;
        MixedFrameBuffer & operator=(const MixedFrameBuffer &) = delete;
        MixedFrameBuffer(MixedFrameBuffer &&) = delete;
        MixedFrameBuffer & operator=(MixedFrameBuffer &&) = delete;
    };

}

#endif /* INCLUDE_NON_ROOT_MIXEDFRAMEBUFFER_H */
