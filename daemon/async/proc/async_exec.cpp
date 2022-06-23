/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "async/proc/async_exec.hpp"

namespace async::proc {

    lib::AutoClosingFd from_stdout(async_process_t & p)
    {
        lib::AutoClosingFd result {std::move(p.get_stdout_read())};
        p.on_output_complete({}, false);
        return result;
    }

    lib::AutoClosingFd from_stderr(async_process_t & p)
    {
        lib::AutoClosingFd result {std::move(p.get_stderr_read())};
        p.on_output_complete({}, true);
        return result;
    }

    boost::system::error_code configure_stdin(std::shared_ptr<async_process_t> const & /*process*/,
                                              detail::discard_tag_t const & /*tag */,
                                              lib::AutoClosingFd & fd)
    {
        fd.close();
        return {};
    }

    boost::system::error_code configure_stdin(std::shared_ptr<async_process_t> const & /*process*/,
                                              detail::ignore_tag_t const & /*tag */,
                                              lib::AutoClosingFd & /*fd*/)
    {
        return {};
    }

    boost::system::error_code configure_stdout_err(std::shared_ptr<async_process_t> const & process,
                                                   detail::ignore_tag_t const & /*tag */,
                                                   bool is_stderr,
                                                   lib::AutoClosingFd & fd)
    {
        if (!fd) {
            process->on_output_complete({}, is_stderr);
        }
        return {};
    }
}
