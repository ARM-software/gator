/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "LineReader.h"

#include "lib/Assert.h"
#include "lib/Syscall.h"

#include <algorithm>
#include <iostream>
#include <system_error>

namespace lib {
    static constexpr std::size_t DEFAULT_LINEBUFFER_CAPACITY = 4096;

    LineReader::LineReader(int file_descriptor) : LineReader(file_descriptor, DEFAULT_LINEBUFFER_CAPACITY)
    {
    }

    LineReader::LineReader(int file_descriptor,  //NOLINT(bugprone-easily-swappable-parameters)
                           std::size_t capacity) //NOLINT(bugprone-easily-swappable-parameters)
        : fd(file_descriptor), capacity(capacity), read_array_size(0)
    {
        read_array.resize(capacity);
    }

    LineReaderResult LineReader::readMoreBytes()
    {
        runtime_assert(line_buffer.size() <= capacity, "unexpected size of line buffer");

        // read at most the amount of space remaining in line_buffer
        const std::size_t remaining_space = capacity - line_buffer.size();
        if (remaining_space == 0) {
            // the line buffer's full, so there's no point reading
            // we wouldn't be able to copy the bytes into the line_buffer.
            // to find further line endings.
            return {std::make_error_code(std::errc::no_buffer_space)};
        };
        const ssize_t read_bytes = lib::read(fd, read_array.data(), remaining_space);

        if (read_bytes < 0) {
            return {std::make_error_code(std::errc::io_error)};
        }

        read_array_size = read_bytes;

        if (read_bytes == 0) {
            return {true};
        }

        auto first = std::cbegin(read_array);
        auto limit = first + read_array_size;
        runtime_assert(limit <= std::cend(read_array), "incorrect read bound");
        line_buffer.append(first, limit);

        return {};
    }

    bool LineReader::evacuateOneLineFromBuffer(std::string & output)
    {
        const auto first = line_buffer.cbegin();
        const auto last = line_buffer.cend();

        auto newline_it = std::find(first, last, '\n');
        if (newline_it == last) {
            return false;
        };

        ++newline_it;
        auto evacuated_len = std::distance(first, newline_it);
        output.append(first, newline_it);

        eraseFromFrontOfBuffer(evacuated_len);

        return true;
    }

    void LineReader::evacuateBuffer(std::string & output)
    {
        const auto first = std::cbegin(line_buffer);
        const auto last = std::cend(line_buffer);
        const auto evacuated_len = std::distance(first, last);
        output.append(first, last);

        eraseFromFrontOfBuffer(evacuated_len);
    }

    void LineReader::eraseFromFrontOfBuffer(std::size_t len)
    {
        const auto first = std::cbegin(line_buffer);
        const auto limit = first + len;
        line_buffer.erase(first, limit);
    }

    LineReaderResult LineReader::readLine(std::string & result)
    {
        while (true) {
            const bool appended_line = evacuateOneLineFromBuffer(result);
            if (appended_line) {
                break;
            }
            LineReaderResult read_attempt = readMoreBytes();

            if (!read_attempt.ok()) {
                if (read_attempt.reached_eof()) {
                    evacuateBuffer(result);
                }
                return read_attempt;
            }
        }
        return {};
    }
}
