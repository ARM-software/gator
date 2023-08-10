/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "logging/agent_log.h"

#include "Logging.h"
#include "lib/AutoClosingFd.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "lib/Time.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

#include <boost/asio/buffer.hpp>
#include <boost/asio/read_until.hpp>

#include <fcntl.h>
#include <unistd.h>

namespace logging {
    namespace {
        constexpr std::string_view message_start_marker {"\x01"};
        constexpr std::string_view message_end_marker {"\x04"};
        constexpr std::string_view separator {"\x09"};

        void write_bytes(int file_descriptor, std::string_view str)
        {
            while (!str.empty()) {
                auto n = ::write(file_descriptor, str.data(), str.size());
                if (n <= 0) {
                    return; // oh well. cant even log :-( an error
                }
                str = str.substr(n);
            }
        }

        void write(int file_descriptor, std::uint32_t n)
        {
            dprintf(file_descriptor, "%" PRIu32, n);
        }
        void write(int file_descriptor, std::int64_t n)
        {
            dprintf(file_descriptor, "%" PRId64, n);
        }
        void write(int file_descriptor, thread_id_t t)
        {
            dprintf(file_descriptor, "%" PRIi32, pid_t(t));
        }
        void write(int file_descriptor, log_level_t l)
        {
            write(file_descriptor, std::uint32_t(l));
        }
        void write(int file_descriptor, std::string_view str)
        {
            std::size_t from = 0;

            for (std::size_t pos = 0; pos < str.size(); ++pos) {
                char chr = str[pos];
                // escape control characters
                if ((chr < ' ') || (chr == '\\')) {
                    // output the preceeding chars in the message since the last escape/start
                    if (from < pos) {
                        write_bytes(file_descriptor, str.substr(from, pos - from));
                    }
                    // encode the char
                    if (chr == '\\') {
                        dprintf(file_descriptor, "\\\\");
                    }
                    else if (chr == '\n') {
                        dprintf(file_descriptor, "\\n");
                    }
                    else {
                        dprintf(file_descriptor, "\\%03" PRIo32, std::uint32_t(std::uint8_t(chr)));
                    }
                    from = pos + 1;
                }
            }

            // output the remaining chars in the message since the last escape/start
            if (from < str.size()) {
                write_bytes(file_descriptor, str.substr(from));
            }
        }

        constexpr bool is_octal(char c)
        {
            return (c >= '0') && (c < '8');
        }

        std::optional<std::int64_t> decode_num(std::string_view s)
        {
            return lib::try_to_int<std::int64_t>(s);
        }

        std::optional<std::string_view> decode_str(char * start, char const * end)
        {
            constexpr std::uint8_t octal_base = 8;

            char * write = start;
            for (char * from = start; from < end;) {
                // is it an escape sequence
                if (*from == '\\') {
                    // is it a numeric encoding?
                    if ((from + 3) <= end) {
                        char oct1 = from[1];
                        char oct2 = from[2];
                        char oct3 = from[3];

                        if (is_octal(oct1) && is_octal(oct2) && is_octal(oct3)) {
                            // decode char and replace
                            *write++ = char(((oct1 - '0') * octal_base * octal_base) + ((oct2 - '0') * octal_base)
                                            + (oct3 - '0'));
                            from += 4;
                            continue;
                        }
                    }

                    // is it an 'n' or '\\'
                    if ((from + 1) <= end) {
                        if (from[1] == '\\') {
                            // replace
                            *write++ = '\\';
                            from += 2;
                            continue;
                        }

                        if (from[1] == 'n') {
                            // replace
                            *write++ = '\n';
                            from += 2;
                            continue;
                        }
                    }

                    // none of the above, must be invalid
                    return {};
                }

                // a normal char
                *write++ = *from++;
            }

            return std::string_view(start, write - start);
        }

        std::optional<std::string_view> decode_str(std::string_view s)
        {
            // we can safely const cast here since we own the std::string that backs the view...
            return decode_str(const_cast<char *>(s.data()), const_cast<char *>(s.data() + s.size()));
        }

        constexpr std::size_t expected_no_fields = 7;
        constexpr std::size_t field_index_level = 0;
        constexpr std::size_t field_index_tid = 1;
        constexpr std::size_t field_index_file = 2;
        constexpr std::size_t field_index_line = 3;
        constexpr std::size_t field_index_secs = 4;
        constexpr std::size_t field_index_nsec = 5;
        constexpr std::size_t field_index_text = 6;

        std::optional<std::array<std::string_view, expected_no_fields>> split_fields(std::string_view inner)
        {
            // between level and tid
            auto from_1 = 0;
            auto sep_1 = inner.find(separator, from_1);
            if (sep_1 == std::string_view::npos) {
                return {};
            }
            // between tid and file
            auto from_2 = sep_1 + separator.size();
            auto sep_2 = inner.find(separator, from_2);
            if (sep_2 == std::string_view::npos) {
                return {};
            }
            // between file and line
            auto from_3 = sep_2 + separator.size();
            auto sep_3 = inner.find(separator, from_3);
            if (sep_3 == std::string_view::npos) {
                return {};
            }
            // between line and seconds
            auto from_4 = sep_3 + separator.size();
            auto sep_4 = inner.find(separator, from_4);
            if (sep_4 == std::string_view::npos) {
                return {};
            }
            // between seconds and nsecs
            auto from_5 = sep_4 + separator.size();
            auto sep_5 = inner.find(separator, from_5);
            if (sep_5 == std::string_view::npos) {
                return {};
            }
            // between nsecs and message
            auto from_6 = sep_5 + separator.size();
            auto sep_6 = inner.find(separator, from_6);
            if (sep_6 == std::string_view::npos) {
                return {};
            }

            // extract the fields
            auto level_str = inner.substr(from_1, sep_1 - from_1);
            auto tid_str = inner.substr(from_2, sep_2 - from_2);
            auto file_str = inner.substr(from_3, sep_3 - from_3);
            auto line_str = inner.substr(from_4, sep_4 - from_4);
            auto secs_str = inner.substr(from_5, sep_5 - from_5);
            auto nsec_str = inner.substr(from_6, sep_6 - from_6);
            auto text_str = inner.substr(sep_6 + separator.size());

            if (level_str.empty() || tid_str.empty() || line_str.empty() || secs_str.empty() || nsec_str.empty()) {
                return {};
            }

            return {{
                level_str,
                tid_str,
                file_str,
                line_str,
                secs_str,
                nsec_str,
                text_str,
            }};
        }

    }

    lib::AutoClosingFd agent_logger_t::get_log_file_fd()
    {
        //NOLINTNEXTLINE(concurrency-mt-unsafe)
        auto const * lfp = getenv("GATORD_LOG_FILE_PATH");
        if (lfp == nullptr) {
            return {};
        }
        auto path = lib::FsEntry::create(lfp);
        if (!path.exists()) {
            return {};
        }
        auto file = lib::FsEntry::create(path, lib::Format() << "gatord-" << getpid() << ".log");
        return lib::AutoClosingFd {open(file.path().c_str(),
                                        // NOLINTNEXTLINE(hicpp-signed-bitwise)
                                        O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
                                        // NOLINTNEXTLINE(hicpp-signed-bitwise)
                                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)};
    }

    void agent_logger_t::log_item(thread_id_t tid,
                                  log_level_t level,
                                  log_timestamp_t const & timestamp,
                                  source_loc_t const & location,
                                  std::string_view message)
    {
        // writing to the log must be serialized in a multithreaded environment
        std::lock_guard lock {mutex};

        // encode the message as a specially escaped and delimited line of text.
        // The encoding leaves the message largely human readable, whilst ensuring it fits on a single line and is recognizable
        // If any other (e.g. library, stl) code happens to printf to stderr, then it will not corrupt the output
        // and the receiver should be able to pick up the log entries + any random output (which will be considered error logging)
        write_bytes(pipe_fd, message_start_marker);
        write(pipe_fd, level);
        write_bytes(pipe_fd, separator);
        write(pipe_fd, tid);
        write_bytes(pipe_fd, separator);
        write(pipe_fd, location.file_name());
        write_bytes(pipe_fd, separator);
        write(pipe_fd, location.line_no());
        write_bytes(pipe_fd, separator);
        write(pipe_fd, timestamp.seconds);
        write_bytes(pipe_fd, separator);
        write(pipe_fd, timestamp.nanos);
        write_bytes(pipe_fd, separator);
        write(pipe_fd, message);
        write_bytes(pipe_fd, message_end_marker);
        write_bytes(pipe_fd, "\n");

        // optional human readable TSV formatted log file
        if (log_file_descriptor) {
            write(*log_file_descriptor, level);
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, tid);
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, location.file_name());
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, location.line_no());
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, timestamp.seconds);
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, timestamp.nanos);
            write_bytes(*log_file_descriptor, "\t");
            write(*log_file_descriptor, message);
            write_bytes(*log_file_descriptor, "\n");
        }
    }

    void agent_log_reader_t::do_async_read()
    {
        using namespace async::continuations;

        spawn("agent-log-reader",
              async::async_consume_all_lines(
                  line_reader,
                  [st = shared_from_this()](std::string_view line) {
                      // process line of text
                      st->do_process_next_line(line);
                  },
                  use_continuation));
    }

    void agent_log_reader_t::do_process_next_line(std::string_view line)
    {
        constexpr std::size_t expected_minimum_size = message_start_marker.size() //
                                                    + 1                           // level (int)
                                                    + separator.size()            //
                                                    + 1                           // tid (int)
                                                    + separator.size()            //
                                                    + 0                           // file (str)
                                                    + separator.size()            //
                                                    + 1                           // line (int)
                                                    + separator.size()            //
                                                    + 1                           // seconds (int)
                                                    + separator.size()            //
                                                    + 1                           // nsec (int)
                                                    + separator.size()            //
                                                    + 0                           // message (str)
                                                    + message_end_marker.size();

        // empty substr means no marker, get more bytes
        if (line.empty()) {
            LOG_TRACE("(%p) No end of line found", this);
            return do_async_read();
        }

        // remove trailing newline (turn it into a null terminator instead so that it can be used with printf)
        if (line.back() == '\n') {
            const_cast<char &>(line.back()) = 0;
            line.remove_suffix(1);
        }

        // ignore empty lines
        if (line.empty()) {
            LOG_TRACE("(%p) Ignoring empty line", this);
            return do_async_read();
        }

        // must have a minimum size
        if (line.size() < expected_minimum_size) {
            return do_unexpected_message(line);
        }

        // does it start with the marker?
        if (line.substr(0, message_start_marker.size()) != message_start_marker) {
            // no, just a normal line of text
            return do_unexpected_message(line);
        }

        // does it end with the marker?
        if (line.substr(line.size() - message_end_marker.size()) != message_end_marker) {
            // no, just a normal line of text
            return do_unexpected_message(line);
        }

        // find the separators and split the fields
        auto inner = line.substr(message_start_marker.size(),
                                 line.size() - (message_start_marker.size() + message_end_marker.size()));

        auto fields_opt = split_fields(inner);
        if (!fields_opt) {
            // no, just a normal line of text
            return do_unexpected_message(line);
        }

        // decode the fields
        auto & fields = *fields_opt;
        auto level_num = decode_num(fields[field_index_level]);
        auto tid_num = decode_num(fields[field_index_tid]);
        auto file = decode_str(fields[field_index_file]);
        auto line_num = decode_num(fields[field_index_line]);
        auto secs_num = decode_num(fields[field_index_secs]);
        auto nsec_num = decode_num(fields[field_index_nsec]);
        auto text = decode_str(fields[field_index_text]);

        // all fields must be valid
        if ((!level_num) || (!tid_num) || (!file) || (!line_num) || (!secs_num) || (!nsec_num) || (!text)) {
            return do_unexpected_message(line);
        }

        // a valid message
        return do_expected_message(thread_id_t(*tid_num),
                                   log_level_t(*level_num),
                                   log_timestamp_t {*secs_num, *nsec_num},
                                   source_loc_t {*file, unsigned(*line_num)},
                                   *text);
    }

    void agent_log_reader_t::do_unexpected_message(std::string_view msg)
    {
        do_expected_message(thread_id_t {0}, log_level_t::error, log_timestamp_t {}, source_loc_t {}, msg);
    }

    void agent_log_reader_t::do_expected_message(thread_id_t tid,
                                                 log_level_t level,
                                                 log_timestamp_t timestamp,
                                                 source_loc_t location,
                                                 std::string_view message)
    {
        consumer(tid, level, timestamp, location, message);
    }
}
