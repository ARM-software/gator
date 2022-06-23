/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#include "lib/Popen.h"

#include "Logging.h"
#include "lib/Assert.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lib {
    struct PopenResult popen(lib::Span<const char * const> command_and_args)
    {
        runtime_assert(command_and_args.size() > 0, "args size must be > 0");
        runtime_assert(command_and_args.back() == nullptr, "lib::popen requires null terminated list");

        int execerr[2];
        int in[2];
        int out[2];
        int err[2];
        // in/out/err pipes should not be CLOEXEC since we need them for the exec'ed child process
        // NOLINTNEXTLINE(android-cloexec-pipe)
        if (pipe2(execerr, O_CLOEXEC) == -1 || pipe(in) == -1 || pipe(out) == -1 || pipe(err) == -1) {
            return {-errno, -1, -1, -1};
        }

        const int pid = fork();
        if (pid < 0) {
            return {-errno, -1, -1, -1};
        }
        if (pid == 0) {
            // child
            close(execerr[0]);
            close(in[1]);
            close(out[0]);
            close(err[0]);

            dup2(in[0], STDIN_FILENO);
            dup2(out[1], STDOUT_FILENO);
            dup2(err[1], STDERR_FILENO);

            close(in[0]);
            close(out[1]);
            close(err[1]);

            // disable buffering
            ::setvbuf(stdout, nullptr, _IONBF, 0);
            ::setvbuf(stderr, nullptr, _IONBF, 0);

            // get sighup if parent exits
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);

            execvp(command_and_args[0], const_cast<char * const *>(command_and_args.data()));
            const int error = errno;
            // try and send the errno, but ignore it if it fails
            const ssize_t num = write(execerr[1], &error, sizeof(error));
            (void) num;
            exit(1);
        }
        else {
            LOG_DEBUG("Forked child process for '%s' has pid %d", command_and_args[0], pid);

            // parent
            close(execerr[1]);
            close(in[0]);
            close(out[1]);
            close(err[1]);

            int error;
            if (read(execerr[0], &error, sizeof(error)) != 0) {
                close(execerr[0]);
                close(in[1]);
                close(out[0]);
                close(err[0]);
                while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {
                }
                return {-error, -1, -1, -1};
            }

            close(execerr[0]);
            return {pid, in[1], out[0], err[0]};
        }
    }

    int pclose(const PopenResult & result)
    {
        if (result.pid < 0) {
            return result.pid;
        }

        int status;
        close(result.in);
        close(result.out);
        close(result.err);
        while (waitpid(result.pid, &status, 0) == -1) {
            if (errno != EINTR) {
                // use INT_MIN because -errno theoretically could go to -INT_MAX == INT_MIN + 1
                return INT_MIN;
            }
        }
        // WIFEXITED and friends only use lowest 16 bits
        // so the mask probably isn't needed but just to be safe
        return status & INT_MAX;
    }

}
