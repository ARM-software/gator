/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "lib/forked_process.h"

#include "ExitStatus.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"
#include "lib/Syscall.h"
#include "lib/error_code_or.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_LOG_ERROR_FD(fd, format, ...) dprintf((fd), (format), ##__VA_ARGS__)
#define CHILD_LOG_ERROR(format, ...) CHILD_LOG_ERROR_FD(STDERR_FILENO, (format), ##__VA_ARGS__)

namespace lib {

    namespace {
        [[noreturn]] void kill_self()
        {
            kill(0, SIGKILL);
            _exit(COMMAND_FAILED_EXIT_CODE);
        }
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    error_code_or_t<forked_process_t> forked_process_t::fork_process(
        bool prepend_command,
        std::string const & cmd,
        lib::Span<std::string const> args,
        boost::filesystem::path const & cwd,
        std::optional<std::pair<uid_t, gid_t>> const & uid_gid,
        stdio_fds_t stdio_fds)
    {
        prepend_command |= args.empty();

        // the current uid/gid and the requested uid/gid
        uid_t const c_uid = geteuid();
        gid_t const c_gid = getegid();
        uid_t const r_uid = (uid_gid ? uid_gid->first : -1);
        gid_t const r_gid = (uid_gid ? uid_gid->second : -1);

        // log the operation
        LOG_FINE("Forking exe '%s' with prepend_command=%u, cwd='%s', uid_gid={%d, %d} vs {%d, %d}",
                 cmd.c_str(),
                 prepend_command,
                 cwd.c_str(),
                 r_uid,
                 r_gid,
                 c_uid,
                 c_gid);
        for (auto const & a : args) {
            LOG_FINE("   ARG: '%s'", a.c_str());
        }

        // this pipe is used to trigger the exec or abort from the parent to the child
        auto exec_abort_or_error = pipe_pair_t::create(O_CLOEXEC);
        auto const * error = get_error(exec_abort_or_error);
        if (error != nullptr) {
            return *error;
        }
        auto exec_abort = get_value(std::move(exec_abort_or_error));

        // create null terminated args vector (before fork to avoid allocating in child in multithreaded environment)
        std::vector<char *> args_null_term_list {};
        args_null_term_list.reserve(args.size() + (prepend_command ? 2 : 1));
        if (prepend_command) {
            args_null_term_list.push_back(const_cast<char *>(cmd.c_str()));
        }
        for (const auto & arg : args) {
            args_null_term_list.push_back(const_cast<char *>(arg.c_str()));
        }
        args_null_term_list.push_back(nullptr);

        char * const * const args_null_term = args_null_term_list.data();

        // right, lets start the child
        auto pid = ::fork();

        if (pid < 0) {
            LOG_WARNING("fork failed with %d", errno);
            return boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));
        }

        if (pid != 0) {
            // parent
            return forked_process_t(std::move(stdio_fds.stdin_write),
                                    std::move(stdio_fds.stdout_read),
                                    std::move(stdio_fds.stderr_read),
                                    std::move(exec_abort.write),
                                    pid);
        }

        // child

        // clear any signal handlers
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGHUP, SIG_DFL);

        // Need to change the GPID so that all children of this process will have this processes PID as their GPID.
        setpgid(pid, pid);

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command"), 0, 0, 0);

        // Close the unused fd's
        stdio_fds.stdin_write.close();
        stdio_fds.stdout_read.close();
        stdio_fds.stderr_read.close();
        exec_abort.write.close();

        if (dup2(stdio_fds.stdin_read.get(), STDIN_FILENO) < 0) {
            CHILD_LOG_ERROR_FD(stdio_fds.stderr_write.get(), "dup2(stdin) failed");
            kill_self();
        }
        if (dup2(stdio_fds.stdout_write.get(), STDOUT_FILENO) < 0) {
            CHILD_LOG_ERROR_FD(stdio_fds.stderr_write.get(), "dup2(stdout) failed");
            kill_self();
        }
        if (dup2(stdio_fds.stderr_write.get(), STDERR_FILENO) < 0) {
            CHILD_LOG_ERROR_FD(stdio_fds.stderr_write.get(), "dup2(stderr) failed");
            kill_self();
        }

        // disable buffering
        ::setvbuf(stdout, nullptr, _IONBF, 0);
        ::setvbuf(stderr, nullptr, _IONBF, 0);

        // get sighup if parent exits
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
            CHILD_LOG_ERROR("prctl(PR_SET_PDEATHSIG, SIGKILL) failed with errno %d", errno);
            kill_self();
        }

        if (setpriority(PRIO_PROCESS, lib::gettid(), 0) == -1) {
            CHILD_LOG_ERROR("setpriority failed with errno %d", errno);
            kill_self();
        }

        if (uid_gid) {
            // failure is only an error if c_uid == 0, i.e. we are root
            if ((setgroups(1, &r_gid) != 0) && (c_uid == 0)) {
                CHILD_LOG_ERROR("setgroups failed, GID %d, with errno %d", r_gid, errno);
                kill_self();
            }
            if ((setresgid(r_gid, r_gid, r_gid) != 0) && (c_uid == 0)) {
                CHILD_LOG_ERROR("setresgid failed, GID %d, with errno %d", r_gid, errno);
                kill_self();
            }
            if ((setresuid(r_uid, r_uid, r_uid) != 0) && (c_uid == 0)) {
                CHILD_LOG_ERROR("setresuid failed, UID %d, with errno %d", r_uid, errno);
                kill_self();
            }
        }

        // change cwd
        if (!cwd.empty()) {
            const char * const path = cwd.c_str();
            if (chdir(path) != 0) {
                CHILD_LOG_ERROR("chdir(\"%s\") failed; aborting.", path);
                kill_self();
            }
        }

        // Wait for exec or abort command.
        forked_process_t::exec_state_t fail_or_exec = forked_process_t::exec_state_t::abort;
        while (read(exec_abort.read.get(), &fail_or_exec, sizeof(fail_or_exec)) < 0) {
            if (errno != EINTR) {
                CHILD_LOG_ERROR("error while reading exec_abort pipe, with errno %d", errno);
                kill_self();
            }
        }

        if (fail_or_exec == forked_process_t::exec_state_t::abort) {
            CHILD_LOG_ERROR("received exce command abort");
            kill_self();
        }

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(args_null_term[0]), 0, 0, 0);
        execvp(cmd.c_str(), args_null_term);

        // execp returns if there is an error
        CHILD_LOG_ERROR("execvp for command failed");
        _exit(errno == ENOENT ? failure_exec_not_found : failure_exec_invalid);
    }

    void forked_process_t::abort()
    {
        AutoClosingFd exec_abort_write {std::move(this->exec_abort_write)};

        if (exec_abort_write) {
            forked_process_t::exec_state_t abort = forked_process_t::exec_state_t::abort;
            while (lib::write(exec_abort_write.get(), &abort, sizeof(char)) < 1) {
                if (errno != EINTR) {
                    LOG_DEBUG("abort... write failed with %d", errno);
                    break;
                }
            }
        }

        auto pid = std::exchange(this->pid, 0);
        if (pid > 0) {
            if (lib::kill(-pid, SIGTERM) == -1) {
                LOG_DEBUG("abort... kill failed with %d", errno);
            }
        }
    };

    [[nodiscard]] bool forked_process_t::exec()
    {
        AutoClosingFd exec_abort_write {std::move(this->exec_abort_write)};

        if (!exec_abort_write) {
            return false;
        }

        forked_process_t::exec_state_t exec = forked_process_t::exec_state_t::go;
        size_t write_result;
        while (write_result = lib::write(exec_abort_write.get(), &exec, sizeof(char)), write_result < 1) {
            if (errno != EINTR) {
                if (write_result == 0) {
                    LOG_DEBUG("exec write failed, forked process has already exited");
                    return false;
                }

                // write_result < 0
                LOG_DEBUG("exec write failed with %d", errno);
                return false;
            }
        }

        return true;
    };
}
