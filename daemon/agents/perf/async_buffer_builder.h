/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "BufferUtils.h"
#include "IRawFrameBuilder.h"
#include "Logging.h"
#include "async/async_buffer.hpp"
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
    class apc_buffer_builder_t : public IRawFrameBuilderWithDirectAccess {
    public:
        explicit apc_buffer_builder_t(BufferType & buffer) : buffer(buffer), write_index(0), start_of_current_frame(0)
        {
        }

        ~apc_buffer_builder_t() override = default;

        //
        // IRawFrameBuilder methods
        //
        /**
         * Buffer starts with FrameType.
         * The Response Type header (eg: for APC, ResponseType::APC_DATA)), is not added to the buffer.
         */
        void beginFrame(FrameType frameType) override
        {
            start_of_current_frame = write_index;

            packInt(static_cast<std::int32_t>(frameType));
        }

        /**
         * Discards any data written in the current frame and resets the write index.
         */
        void abortFrame() override
        {
            write_index = start_of_current_frame;
            buffer.resize(write_index);
        }

        /**
         * Finishes the current frame. The buffer will no include the message length prefix.
         */
        void endFrame() override
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
         * Flush is not required for this implementation as the whole buffer is either committed
         * or discarded when the builder instance is disposed.
         *
         * @return Always returns false.
         */
        bool needsFlush() override { return false; }

        /**
         * Not needed for this implementation. This is a no-op.
         */
        void flush() override {}

        [[nodiscard]] int bytesAvailable() const override
        {
            std::size_t available = buffer.max_size() - write_index;
            if (available > std::numeric_limits<int>::max()) {
                return std::numeric_limits<int>::max();
            }
            return static_cast<int>(available);
        }

        int packInt(std::int32_t x) override
        {
            ensure_space_at(write_index, buffer_utils::MAXSIZE_PACK32);
            return buffer_utils::packInt(buffer.data(), write_index, x);
        }

        int packInt64(std::int64_t x) override
        {
            ensure_space_at(write_index, buffer_utils::MAXSIZE_PACK64);
            return buffer_utils::packInt64(buffer.data(), write_index, x);
        }

        void writeBytes(const void * data, std::size_t count) override
        {
            if (count == 0) {
                return;
            }

            ensure_space_at(write_index, count);

            ::memcpy(buffer.data() + write_index, data, count);
            write_index += count;
        }

        void writeString(std::string_view str) override
        {
            auto len = str.size();
            if (len > std::numeric_limits<int>::max()) {
                len = std::numeric_limits<int>::max();
            }
            packInt(static_cast<int>(len));
            writeBytes(str.data(), len);
        }

        void waitForSpace(int bytes) override
        {
            if (!supportsWriteOfSize(bytes)) {
                runtime_assert(false, "Attempted to overflow apc_buffer_builder_t size");
            }
        }

        [[nodiscard]] bool supportsWriteOfSize(int bytes) const override
        {
            if (bytes < 0) {
                return false;
            }
            return static_cast<std::size_t>(bytes) <= buffer.max_size() - static_cast<std::size_t>(write_index);
        }

        //
        // IRawFrameBuilderWithDirectAccess methods
        //

        [[nodiscard]] int getWriteIndex() const override { return write_index; }

        void advanceWrite(int bytes) override
        {
            ensure_space_at(write_index, bytes);
            write_index += bytes;
        }

        void writeDirect(int index, const void * data, std::size_t count) override
        {
            if (count == 0) {
                return;
            }

            ensure_space_at(index, count);
            ::memcpy(buffer.data() + index, data, count);
        }

    private:
        // number of bytes in a frame header. frames will need to be bigger than
        // this to be committed to the buffer
        static constexpr int frame_header_size = 1;

        BufferType & buffer;
        int write_index;
        int start_of_current_frame;

        void ensure_space_at(int pos, int bytes)
        {
            runtime_assert(pos + bytes > 0, "Size must not be negative");
            const auto request_size = static_cast<std::size_t>(pos + bytes);
            runtime_assert(request_size <= buffer.max_size(), "Cannot grow apc_buffer_builder_t past its limit");
            if (buffer.size() < request_size) {
                buffer.resize(request_size);
            }
        }
    };

    /**
     * An adapter that allows an async::async_buffer_t to be used as an APC frame builder.
     */
    class async_buffer_builder_t : public IRawFrameBuilderWithDirectAccess {
    public:
        /**
         * Constructs an async_buffer_builder_t that wraps the specified async_buffer_t.
         * The commit_action_t is used to commit or discard the underlying buffer based on
         * whether any frames were written out.
         */
        async_buffer_builder_t(async::async_buffer_t::mutable_buffer_type buffer,
                               async::async_buffer_t::commit_action_t commit_action)
            : writer(buffer), builder(writer), commit_action(std::move(commit_action))
        {
        }

        async_buffer_builder_t(const async_buffer_builder_t &) = delete;
        async_buffer_builder_t & operator=(const async_buffer_builder_t &) = delete;

        ~async_buffer_builder_t() override
        {
            const auto size = builder.getWriteIndex();
            if (size > 0) {
                boost::system::error_code ec {};
                if (!commit_action.commit(ec, size)) {
                    LOG_ERROR("Failed to commit %d bytes to async_buffer_t: %s", size, ec.message().c_str());
                }
            }
            else {
                commit_action.discard();
            }
        }

        //
        // IRawFrameBuilder methods
        //
        /**
         * @copydoc apc_buffer_builder_t::beginFrame(FrameType)
         */
        void beginFrame(FrameType frameType) override { builder.beginFrame(frameType); }

        /**
         * @copydoc apc_buffer_builder_t::abortFrame()
         */
        void abortFrame() override { builder.abortFrame(); }

        /**
         * @copydoc apc_buffer_builder_t::endFrame()
         */
        void endFrame() override { builder.endFrame(); }

        /**
         * @copydoc apc_buffer_builder_t::needsFlush()
         */
        bool needsFlush() override { return builder.needsFlush(); }

        /**
         * @copydoc apc_buffer_builder_t::flush()
         */
        void flush() override { builder.flush(); }

        /**
         * @copydoc apc_buffer_builder_t::bytesAvailable()
         */
        [[nodiscard]] int bytesAvailable() const override { return builder.bytesAvailable(); }

        /**
         * @copydoc apc_buffer_builder_t::packInt(std::int32_t)
         */
        int packInt(std::int32_t x) override { return builder.packInt(x); }

        /**
         * @copydoc apc_buffer_builder_t::packInt65(std::int64_t)
         */
        int packInt64(std::int64_t x) override { return builder.packInt64(x); }

        /**
         * @copydoc apc_buffer_builder_t::writeBytes(const void*, std::size_t)
         */
        void writeBytes(const void * data, std::size_t count) override { return builder.writeBytes(data, count); }

        /**
         * @copydoc apc_buffer_builder_t::writeString(std::string_view)
         */
        void writeString(std::string_view str) override { return builder.writeString(str); }
        /**
         * @copydoc apc_buffer_builder_t::waitForSpace(int)
         */
        void waitForSpace(int bytes) override { builder.waitForSpace(bytes); }

        /**
         * @copydoc apc_buffer_builder_t::supportsWriteOfSize(int)
         */
        [[nodiscard]] bool supportsWriteOfSize(int bytes) const override { return builder.supportsWriteOfSize(bytes); }

        //
        // IRawFrameBuilderWithDirectAccess methods
        //

        /**
         * @copydoc apc_buffer_builder_t::getWriteIndex()
         */
        [[nodiscard]] int getWriteIndex() const override { return builder.getWriteIndex(); }

        /**
         * @copydoc apc_buffer_builder_t::advanceWrite(int)
         */
        void advanceWrite(int bytes) override { builder.advanceWrite(bytes); }

        /**
         * @copydoc apc_buffer_builder_t::writeDirect(int, const void *, std::size_t)
         */
        void writeDirect(int index, const void * data, std::size_t count) override
        {
            builder.writeDirect(index, data, count);
        }

    private:
        class char_span_writer_t {
        public:
            explicit char_span_writer_t(lib::Span<char> span) : span(span), write_pointer(0) {}

            [[nodiscard]] char * data() { return span.data(); }

            [[nodiscard]] std::size_t size() const { return span.size(); }

            [[nodiscard]] std::size_t max_size() const { return span.size(); }

            void resize(std::size_t size) { write_pointer = std::min(size, span.size()); }

        private:
            lib::Span<char> span;
            std::size_t write_pointer;
        };

        char_span_writer_t writer;
        apc_buffer_builder_t<char_span_writer_t> builder;
        async::async_buffer_t::commit_action_t commit_action;
    };
}
