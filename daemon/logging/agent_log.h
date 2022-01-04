/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "lib/AutoClosingFd.h"

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/streambuf.hpp>

namespace logging {
    /** Implements log_sink_t for agent sub-processes that log out via the IPC channel */
    class agent_log_sink_t : public log_sink_t {
    public:
        explicit constexpr agent_log_sink_t(int file_descriptor) : file_descriptor(file_descriptor) {}

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        void set_debug_enabled(bool /*enabled*/) override
        { /*ignored*/
        }

        /** Store some log item to the log */
        void log_item(log_level_t level,
                      log_timestamp_t const & timestamp,
                      source_loc_t const & location,
                      std::string_view message) override;

    private:
        /** To protect against concurrect modifications */
        mutable std::mutex mutex {};
        /** The file descriptor to write to */
        int file_descriptor;
    };

    /** An async reader of lines of agent log */
    class agent_log_reader_t : public std::enable_shared_from_this<agent_log_reader_t> {
    public:
        static std::shared_ptr<agent_log_reader_t> create(
            boost::asio::io_context & io_context,
            lib::AutoClosingFd && fd,
            std::function<void(log_level_t, log_timestamp_t, source_loc_t, std::string_view)> consumer)
        {
            auto result = std::make_shared<agent_log_reader_t>(io_context, std::move(fd), std::move(consumer));

            result->do_async_read();

            return result;
        }

        agent_log_reader_t(boost::asio::io_context & io_context,
                           lib::AutoClosingFd && fd,
                           std::function<void(log_level_t, log_timestamp_t, source_loc_t, std::string_view)> consumer)
            : in(io_context, fd.release()), consumer(std::move(consumer))
        {
        }

    private:
        boost::asio::posix::stream_descriptor in;
        std::function<void(log_level_t, log_timestamp_t, source_loc_t, std::string_view)> consumer;
        boost::asio::streambuf buffer {};

        /** Read the next line of data from the stream */
        void do_async_read();

        /** Process the received line */
        void do_process_next_line(std::size_t n);

        /** Handle the line having an unexpected format */
        void do_unexpected_message(std::size_t n_to_consume, std::string_view msg);

        /** Handle the decoded log item */
        void do_expected_message(std::size_t n_to_consume,
                                 log_level_t level,
                                 log_timestamp_t timestamp,
                                 source_loc_t location,
                                 std::string_view message);
    };
}
