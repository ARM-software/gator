/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "lib/Popen.h"

#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>

namespace lib
{
    struct PopenResult popen(const char * const command[])
    {
        int execerr[2];
        int in[2];
        int out[2];
        int err[2];
        if (pipe2(execerr, O_CLOEXEC) == -1 || pipe(in) == -1 || pipe(out) == -1 || pipe(err) == -1) {
            return {-errno, -1, -1, -1};
        }

        const int pid = fork();
        if (pid < 0) {
            return {-errno, -1, -1, -1};
        }
        else if (pid == 0) {
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

            execvp(command[0], const_cast<char * const *>(command));
            const int error = errno;
            // try and send the errno, but ignore it if it fails
            const ssize_t num = write(execerr[1], &error, sizeof(error));
            (void) num;
            exit(1);
        }
        else {
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
                while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR)
                    ;
                return {-error, -1, -1, -1};
            }

            close(execerr[0]);
            return {pid, in[1], out[0], err[0]};
        }
    }

    int pclose(const PopenResult & result)
    {
        if (result.pid < 0)
            return result.pid;

        int status;
        close(result.in);
        close(result.out);
        close(result.err);
        while (waitpid(result.pid, &status, 0) == -1) {
            if (errno != EINTR)
                // use INT_MIN because -errno theoretically could go to -INT_MAX == INT_MIN + 1
                return INT_MIN;
        }
        // WIFEXITED and friends only use lowest 16 bits
        // so the mask probably isn't needed but just to be safe
        return status & INT_MAX;
    }

}

