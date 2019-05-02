/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>

#include "Logging.h"


namespace lib
{

    int pipe_cloexec(int pipefd[2])
    {
        if (pipe(pipefd) != 0) {
            return -1;
        }

        int fdf;
        if (((fdf = fcntl(pipefd[0], F_GETFD)) == -1) || (fcntl(pipefd[0], F_SETFD, fdf | FD_CLOEXEC) != 0)
                || ((fdf = fcntl(pipefd[1], F_GETFD)) == -1) || (fcntl(pipefd[1], F_SETFD, fdf | FD_CLOEXEC) != 0)) {
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
        return 0;
    }

    bool setNonblock(const int fd)
    {
        int flags;

        flags = fcntl(fd, F_GETFL);
        if (flags < 0) {
            logg.logMessage("fcntl getfl failed");
            return false;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            logg.logMessage("fcntl setfl failed");
            return false;
        }

        return true;
    }

    bool writeAll(const int fd, const void * const buf, const size_t pos)
    {
        size_t written = 0;
        while (written < pos) {
            ssize_t bytes = write(fd, reinterpret_cast<const uint8_t *>(buf) + written, pos - written);
            if (bytes <= 0) {
                logg.logMessage("write failed");
                return false;
            }
            written += bytes;
        }

        return true;
    }

    bool readAll(const int fd, void * const buf, const size_t count)
    {
        size_t pos = 0;
        while (pos < count) {
            ssize_t bytes = read(fd, reinterpret_cast<uint8_t *>(buf) + pos, count - pos);
            if (bytes <= 0) {
                logg.logMessage("read failed");
                return false;
            }
            pos += bytes;
        }

        return true;
    }

    bool skipAll(const int fd, const size_t count)
    {
        uint8_t buf[4096];
        size_t pos = 0;
        while (pos < count) {
            const size_t nToRead = std::min<size_t>(sizeof(buf), count - pos);
            ssize_t bytes = read(fd, buf, nToRead);
            if (bytes <= 0) {
                logg.logMessage("skip failed");
                return false;
            }
            pos += bytes;
        }

        return true;
    }
}

