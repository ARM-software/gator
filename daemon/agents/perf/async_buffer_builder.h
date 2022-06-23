/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "BufferUtils.h"
#include "IRawFrameBuilder.h"
#include "Logging.h"
#include "lib/Assert.h"
#include "lib/Span.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <iterator>
#include <limits>

#include <boost/system/error_code.hpp>

namespace agents::perf {

    /**
     * Wraps a vector-like object and allows it to be used as an IRawFrameBuilderWithDirectAccess.
     * The wrapped object is expected to implement the following methods:
     *
     * @code
     * char * data()
     *      - allows direct, mutable access to a linear array of bytes in the buffer.
     *
     * size_t size()   and    size_t max_size()
     *      - used to query the underlying buffer's current size and maximum extent.
     *
     * void resize(size_t)
     *      - increases or decreases the buffer's size. I.e. the result of calling
     *        the size() method will change.
     * @endcode
     */
    template<typename BufferType>
    class apc_buffer_builder_t {
    public:
        explicit apc_buffer_builder_t(BufferType & buffer) : buffer(buffer), write_index(0), start_of_current_frame(0)
        {
        }

        /**
        * Begins a new frame
        *
        * There must be no current frame
        */
        void beginFrame(FrameType frameType)
        {
            start_of_current_frame = write_index;

            packInt(static_cast<std::int32_t>(frameType));
        }

        /**
        * Aborts the current frame
        *
        * There must be a current frame
        * There will be no current frame afterwards
        */
        void abortFrame()
        {
            write_index = start_of_current_frame;
            buffer.resize(write_index);
        }

        /**
        * Ends the current frame and commits it to the buffer
        *
        * There must be a current frame
        * There will be no current frame afterwards
        * Does not flush the buffer
        */
        void endFrame()
        {
            auto payload_length = write_index - start_of_current_frame;
            if (payload_length <= frame_header_size) {
                // nothing was written so discard the frame
                abortFrame();
            }
            else {
                buffer.resize(write_index);
            }
        }

        /**
        * Gets the number of bytes available in the backing buffer
        */
        [[nodiscard]] std::size_t bytesAvailable() const { return buffer.max_size() - write_index; }

        /**
        * Packs a 32 bit number
        *
        * Must be required bytes available
        */
        std::size_t packInt(std::int32_t x)
        {
            ensure_space_at(write_index, buffer_utils::MAXSIZE_PACK32);
            int ignored = 0; // we don't use the wrapping feature + std::size_t != int
            std::size_t n = buffer_utils::packInt(buffer.data() + write_index, ignored, x);
            write_index += n;
            return n;
        }

        /**
        * Packs a 32 bit number
        *
        * Must be required bytes available
        */
        std::size_t packInt(std::uint32_t x) { return packInt(std::int32_t(x)); }

        /**
        * Packs a 64 bit number
        *
        * Must be required bytes available
        */
        std::size_t packInt64(std::int64_t x)
        {
            ensure_space_at(write_index, buffer_utils::MAXSIZE_PACK64);
            int ignored = 0; // we don't use the wrapping feature + std::size_t != int
            std::size_t n = buffer_utils::packInt64(buffer.data() + write_index, ignored, x);
            write_index += n;
            return n;
        }

        /**
        * Packs a 64 bit number
        *
        * Must be required bytes available
        */
        std::size_t packInt64(std::uint64_t x) { return packInt64(std::int64_t(x)); }

        /**
        * Packs a size_t number
        *
        * Must be required bytes available
        */
        std::size_t packIntSize(std::size_t x)
        {
            if constexpr (sizeof(std::size_t) <= sizeof(std::uint32_t)) {
                return packInt(std::uint32_t(x));
            }
            else {
                return packInt64(std::uint64_t(x));
            }
        }

        /**
        * Packs a monotonic_delta_t
        *
        * Must be required bytes available
        */
        std::size_t packMonotonicDelta(monotonic_delta_t x) { return packInt64(std::uint64_t(x)); }

        /** Write a 32-bit unsigned int in little endian form */
        void writeLeUint32(std::uint32_t n)
        {
            std::array<char, 4> const buffer {char(n), char(n >> 8U), char(n >> 16U), char(n >> 24U)};
            writeBytes(buffer.data(), buffer.size());
        }

        /** Write a 32-bit unsigned int in little endian form */
        void writeLeUint32At(std::size_t index, std::uint32_t n)
        {
            std::array<char, 4> const buffer {char(n), char(n >> 8U), char(n >> 16U), char(n >> 24U)};
            writeDirect(index, buffer.data(), buffer.size());
        }

        /**
        * Writes some arbitrary bytes to the frame
        *
        * Must be required bytes available
        */
        void writeBytes(const void * data, std::size_t count)
        {
            if (count == 0) {
                return;
            }

            ensure_space_at(write_index, count);

            ::memcpy(buffer.data() + write_index, data, count);
            write_index += count;
        }

        /**
        * Writes a string to the frame
        *
        * Must be required bytes available
        */
        void writeString(std::string_view str)
        {
            auto len = str.size();
            if (len > std::numeric_limits<int>::max()) {
                len = std::numeric_limits<int>::max();
            }
            packInt(static_cast<int>(len));
            writeBytes(str.data(), len);
        }

        /** Checks if it is possible to write a block of the given size to this buffer
        *
        * @param bytes Number of bytes to check
        * @return True if it is possible, or false if would always fail
        */
        [[nodiscard]] bool supportsWriteOfSize(std::size_t bytes) const
        {
            if (bytes < 0) {
                return false;
            }
            return bytes <= (buffer.max_size() - getWriteIndex());
        }

        /** @return The raw write index */
        [[nodiscard]] std::size_t getWriteIndex() const { return write_index; }

        /** Skip the write index forward by 'bytes' */
        void advanceWrite(std::size_t bytes)
        {
            ensure_space_at(write_index, bytes);
            write_index += bytes;
        }

        /** Write directly into the buffer */
        void writeDirect(std::size_t index, const void * data, std::size_t count)
        {
            if (count == 0) {
                return;
            }

            ensure_space_at(index, count);
            ::memcpy(buffer.data() + index, data, count);
        }

        void trimTo(std::size_t size)
        {
            runtime_assert(size <= write_index, "trimTo cannot extend the buffer");

            buffer.resize(size);
            write_index = size;
        }

    private:
        // number of bytes in a frame header. frames will need to be bigger than
        // this to be committed to the buffer
        static constexpr std::size_t frame_header_size = 1;

        BufferType & buffer;
        std::size_t write_index;
        std::size_t start_of_current_frame;

        void ensure_space_at(std::size_t pos, std::size_t bytes)
        {
            runtime_assert(pos + bytes > 0, "Size must not be negative");
            const auto request_size = (pos + bytes);
            runtime_assert(request_size <= buffer.max_size(), "Cannot grow apc_buffer_builder_t past its limit");
            if (buffer.size() < request_size) {
                buffer.resize(request_size);
            }
        }
    };
}
