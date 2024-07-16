/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "Span.h"
#include "lib/AutoClosingFd.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process_utils.h"

#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>

#include <fcntl.h>

namespace lib {

    /**
     * Represents a forked process, that may subsequently be exec'd
     */
    class forked_process_t {
    public:
        enum class exec_state_t : char { abort, go };

        // these match the exit codes that the shell would for invalid exe and file not found
        static constexpr int failure_exec_invalid = 126;
        static constexpr int failure_exec_not_found = 127;

        forked_process_t() = default;
        forked_process_t(forked_process_t const &) = delete;
        forked_process_t & operator=(forked_process_t const &) = delete;

        forked_process_t(forked_process_t && that) noexcept
            : stdin_write(std::move(that.stdin_write)),
              stdout_read(std::move(that.stdout_read)),
              stderr_read(std::move(that.stderr_read)),
              exec_abort_write(std::move(that.exec_abort_write)),
              pid(std::exchange(that.pid, 0))
        {
        }

        forked_process_t & operator=(forked_process_t && that) noexcept
        {
            if (this != &that) {
                forked_process_t tmp {std::move(that)};
                std::swap(this->stdin_write, tmp.stdin_write);
                std::swap(this->stdout_read, tmp.stdout_read);
                std::swap(this->stderr_read, tmp.stderr_read);
                std::swap(this->exec_abort_write, tmp.exec_abort_write);
                std::swap(this->pid, tmp.pid);
            }
            return *this;
        }

        ~forked_process_t() noexcept { abort(); }

        /** @return True if the process was constructed successfully */
        [[nodiscard]] explicit operator bool() const { return (pid != 0); }

        /** Abort the command that was execvp, send SIGTERM to the command and any children */
        void abort();

        /** Will make the forked child process stop waiting and exec the command */
        [[nodiscard]] bool exec();

        /** @return the write end of the process's stdin (may be closed if not reading stdin, or moved out for use elsewhere) */
        [[nodiscard]] AutoClosingFd & get_stdin_write() { return stdin_write; }

        /** @return the read end of the process's stdout (may be closed if redirected to a file, or moved out for use elsewhere) */
        [[nodiscard]] AutoClosingFd & get_stdout_read() { return stdout_read; }

        /** @return the read end of the process's stderr (may be closed if redirected to a file, or moved out for use elsewhere) */
        [[nodiscard]] AutoClosingFd & get_stderr_read() { return stderr_read; }

        /** @return The pid of the forked process */
        [[nodiscard]] pid_t get_pid() const { return pid; }

        /**
         * Fork a process and returns the forked_process_t if created without any error. Returns errno in case of an error.
         * Child process forked will wait for a notification from the caller to start the commad
         * This is done by calling the exec() on forked_process_t created.
         */
        static error_code_or_t<forked_process_t> fork_process(bool prepend_command,
                                                              std::string const & cmd,
                                                              lib::Span<std::string const> args,
                                                              boost::filesystem::path const & cwd,
                                                              std::optional<std::pair<uid_t, gid_t>> const & uid_gid,
                                                              stdio_fds_t stdio_fds,
                                                              bool create_process_group = false);

        /** Constructor */
        forked_process_t(AutoClosingFd && stdin_write,
                         AutoClosingFd && stdout_read,
                         AutoClosingFd && stderr_read,
                         AutoClosingFd && exec_abort_write,
                         pid_t pid)
            : stdin_write(std::move(stdin_write)),
              stdout_read(std::move(stdout_read)),
              stderr_read(std::move(stderr_read)),
              exec_abort_write(std::move(exec_abort_write)),
              pid(pid)
        {
        }

    private:
        AutoClosingFd stdin_write;
        AutoClosingFd stdout_read;
        AutoClosingFd stderr_read;
        AutoClosingFd exec_abort_write;
        pid_t pid {0};
    };
}
