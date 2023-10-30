/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "async/async_line_reader.hpp"
#include "lib/AutoClosingFd.h"
#include "logging/logger_t.h"

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

namespace logging {
    /** Implements logger_t for agent sub-processes that log out via the IPC channel */
    class agent_logger_t : public logger_t {
    public:
        /** Allocate an optional log file fd for this process */
        static lib::AutoClosingFd get_log_file_fd();

        explicit agent_logger_t(int pipe_fd, lib::AutoClosingFd log_file_descriptor = {})
            : pipe_fd(pipe_fd), log_file_descriptor(std::move(log_file_descriptor))
        {
        }

        /** Toggle whether TRACE/DEBUG/SETUP messages are output to the console */
        void set_debug_enabled(bool /*enabled*/) override
        { /*ignored*/
        }

        /** Toggle whether fine messages are output to the console */
        void set_fine_enabled(bool /*enabled*/) override
        { /*ignored*/
        }

        /** Store some log item to the log */
        void log_item(thread_id_t tid,
                      log_level_t level,
                      log_timestamp_t const & timestamp,
                      source_loc_t const & location,
                      std::string_view message) override;

    private:
        /** To protect against concurrect modifications */
        mutable std::mutex mutex {};
        /** The file descriptor to write to */
        int pipe_fd;
        /** The additional log file descriptor */
        lib::AutoClosingFd log_file_descriptor;
    };

    /** An async reader of lines of agent log */
    class agent_log_reader_t : public std::enable_shared_from_this<agent_log_reader_t> {
    public:
        using consumer_fn_t =
            std::function<void(thread_id_t, log_level_t, log_timestamp_t, source_loc_t, std::string_view)>;

        static std::shared_ptr<agent_log_reader_t> create(boost::asio::io_context & io_context,
                                                          lib::AutoClosingFd && fd,
                                                          consumer_fn_t consumer)
        {
            auto result = std::make_shared<agent_log_reader_t>(io_context, std::move(fd), std::move(consumer));

            result->do_async_read();

            return result;
        }

        agent_log_reader_t(boost::asio::io_context & io_context, lib::AutoClosingFd && fd, consumer_fn_t consumer)
            : consumer(std::move(consumer)),
              line_reader(std::make_shared<async::async_line_reader_t>(
                  boost::asio::posix::stream_descriptor {io_context, fd.release()}))
        {
        }

    private:
        consumer_fn_t consumer;
        std::shared_ptr<async::async_line_reader_t> line_reader;

        /** Read the next line of data from the stream */
        void do_async_read();

        /** Process the received line */
        void do_process_next_line(std::string_view line);

        /** Handle the line having an unexpected format */
        void do_unexpected_message(std::string_view msg);

        /** Handle the decoded log item */
        void do_expected_message(thread_id_t tid,
                                 log_level_t level,
                                 log_timestamp_t timestamp,
                                 source_loc_t location,
                                 std::string_view message);
    };
}
