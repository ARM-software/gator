/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "lib/AutoClosingFd.h"
#include "lib/error_code_or.hpp"

#include <fcntl.h>

namespace lib {
    /** Represents a pair of file descriptors that represent the read and write end of a pipe.
     * For cases where io is redirected to/from a file, then either the read/write end of the pair may be invalid fd */
    struct pipe_pair_t {
        // NOLINTNEXTLINE(hicpp-signed-bitwise) - 0644
        static constexpr int default_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

        AutoClosingFd read;
        AutoClosingFd write;

        static error_code_or_t<pipe_pair_t> create(int flags);
        static error_code_or_t<pipe_pair_t> from_file(char const * path);
        static error_code_or_t<pipe_pair_t> to_file(char const * path, bool truncate = true, int mode = default_mode);
    };

    /**
     * The set of all fds required for a forked process, being stdin, stdout, and stderr.
     */
    struct stdio_fds_t {
        AutoClosingFd stdin_read;
        AutoClosingFd stdin_write;
        AutoClosingFd stdout_read;
        AutoClosingFd stdout_write;
        AutoClosingFd stderr_read;
        AutoClosingFd stderr_write;

        constexpr stdio_fds_t() = default;

        stdio_fds_t(AutoClosingFd stdin_read,
                    AutoClosingFd stdin_write,
                    AutoClosingFd stdout_read,
                    AutoClosingFd stdout_write,
                    AutoClosingFd stderr_read,
                    AutoClosingFd stderr_write)
            : stdin_read(std::move(stdin_read)),
              stdin_write(std::move(stdin_write)),
              stdout_read(std::move(stdout_read)),
              stdout_write(std::move(stdout_write)),
              stderr_read(std::move(stderr_read)),
              stderr_write(std::move(stderr_write))
        {
        }

        stdio_fds_t(pipe_pair_t stdin_pair, pipe_pair_t stdout_pair, pipe_pair_t stderr_pair)
            : stdin_read(std::move(stdin_pair.read)),
              stdin_write(std::move(stdin_pair.write)),
              stdout_read(std::move(stdout_pair.read)),
              stdout_write(std::move(stdout_pair.write)),
              stderr_read(std::move(stderr_pair.read)),
              stderr_write(std::move(stderr_pair.write))
        {
        }

        /** Create all io fds from pipes */
        static error_code_or_t<stdio_fds_t> create_pipes();
        /** Create from the provided pairs */
        static error_code_or_t<stdio_fds_t> create_from(error_code_or_t<pipe_pair_t> stdin_pair,
                                                        error_code_or_t<pipe_pair_t> stdout_pair,
                                                        error_code_or_t<pipe_pair_t> stderr_pair);
    };
}
