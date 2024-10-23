/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */
#pragma once
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace lib {

    /**
     * @brief Returned by the LineReader.  Intended as a means to communicate to the caller
     * when to stop iterating requesting more lines.
     */
    class LineReaderResult {
    public:
        LineReaderResult() = default;
        LineReaderResult(bool reached_end) : reached_end_of_stream(reached_end) {}
        LineReaderResult(std::error_code errc) : err(errc) {}

        /**
         * @brief Intended as the means to communicate to the caller
         * when to stop requesting more lines.
         * @return true if more lines might be returned by the line reader.
         * @return false if no more data will be returned by the line reader.
         */
        [[nodiscard]] bool ok() const { return !reached_end_of_stream && err.value() == 0; }

        /**
         * @brief Returns whether the line reader reached the end of the stream.
         * @return true if end of stream was reached.
         * @return false otherwise.
         */
        [[nodiscard]] bool reached_eof() const { return reached_end_of_stream; }

        /**
         * @brief Returns whether an error was encountered when attempting to read lines.
         * @return std::error_code.value() == 0 if no problem was encountered.
         * @return std::error_code.value() != 0 where there was a problem.
         */
        [[nodiscard]] std::error_code const & error_code() const { return err; }

    private:
        bool reached_end_of_stream {false};
        std::error_code err {};
    };

    /**
     * @brief Instrument to extract lines in cases where only a file descriptor is known,
     * (e.g. because we're reading from a pipe) so the typical get_line functions cannot
     * be used.
     *
     * This implementation limits the size of the buffer used to scan for a newline character.
     * The buffer will not grow indefinitely.  This is to prevent memory exhaustion
     * in the case where there is never a newline in the stream.
     */
    class LineReader {
    public:
        LineReader(int file_descriptor);

        /**
         * @brief Construct a new Line Reader object
         *
         * @param file_descriptor The file descriptor to read from.
         * @param max_buffer_size The maximum size the internal buffer will grow to.
         * Callers should choose this limit to be larger than the maximum expected
         * length of a line.
         */
        LineReader(int file_descriptor, std::size_t capacity);

        /**
         * @brief Appends a line to the supplied string.

         * This method appends only.  The line appended will have a '\n' character on the end, unless
         * we reached the end of the stream and there was no new line character in the stream.
         *
         * @param result The string to append to.
         * @return LineReaderResult represents whether the caller can call readLine again.
         */
        [[nodiscard]] LineReaderResult readLine(std::string & result);

    private:
        int fd;
        std::size_t capacity;
        std::size_t read_array_size;
        std::vector<uint8_t> read_array;
        std::string line_buffer;

        LineReaderResult readMoreBytes();
        bool evacuateOneLineFromBuffer(std::string & output);
        void evacuateBuffer(std::string & output);
        void eraseFromFrontOfBuffer(std::size_t len);
    };
}
