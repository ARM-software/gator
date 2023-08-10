/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "lib/forked_process_utils.h"

#include "Logging.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace lib {

    error_code_or_t<pipe_pair_t> pipe_pair_t::create(int flags)
    {
        std::array<int, 2> fds {{-1, -1}};

        if (pipe2(fds, flags) != 0) {
            auto const e = errno;
            LOG_WARNING("pipe2 failed with %d", errno);
            return boost::system::errc::make_error_code(boost::system::errc::errc_t(e));
        }

        return pipe_pair_t {AutoClosingFd {fds[0]}, AutoClosingFd {fds[1]}};
    }

    error_code_or_t<pipe_pair_t> pipe_pair_t::from_file(char const * path)
    {
        // NOLINTNEXTLINE(android-cloexec-open) - cloexec is not appropriate for fork/exec redirection :-)
        AutoClosingFd fd {::open(path, O_RDONLY)};
        if (!fd) {
            auto const e = errno;
            LOG_WARNING("open failed with %d", errno);
            return boost::system::errc::make_error_code(boost::system::errc::errc_t(e));
        }

        return pipe_pair_t {std::move(fd), {}};
    }

    error_code_or_t<pipe_pair_t> pipe_pair_t::to_file(char const * path, bool truncate, int mode)
    {
        // NOLINTNEXTLINE(hicpp-signed-bitwise,android-cloexec-open)
        AutoClosingFd fd {::open(path, O_WRONLY | O_CREAT | (truncate ? O_TRUNC : 0), mode)};
        if (!fd) {
            auto const e = errno;
            LOG_WARNING("open failed with %d", errno);
            return boost::system::errc::make_error_code(boost::system::errc::errc_t(e));
        }

        return pipe_pair_t {{}, std::move(fd)};
    }

    error_code_or_t<stdio_fds_t> stdio_fds_t::create_pipes()
    {
        return create_from(pipe_pair_t::create(0), pipe_pair_t::create(0), pipe_pair_t::create(0));
    }

    error_code_or_t<stdio_fds_t> stdio_fds_t::create_from(error_code_or_t<pipe_pair_t> stdin_pair,
                                                          error_code_or_t<pipe_pair_t> stdout_pair,
                                                          error_code_or_t<pipe_pair_t> stderr_pair)
    {
        auto const * error = get_error(stdin_pair);
        if (error != nullptr) {
            return *error;
        }

        error = get_error(stdout_pair);
        if (error != nullptr) {
            return *error;
        }

        error = get_error(stderr_pair);
        if (error != nullptr) {
            return *error;
        }

        return stdio_fds_t {
            get_value(std::move(stdin_pair)),
            get_value(std::move(stdout_pair)),
            get_value(std::move(stderr_pair)),
        };
    }
}
