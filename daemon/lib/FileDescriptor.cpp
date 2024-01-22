/* Copyright (C) 2018-2023 by Arm Limited. All rights reserved. */

#include "Logging.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

namespace lib {

    int pipe_cloexec(int pipefd[2])
    {
        const int result = pipe2(pipefd, O_CLOEXEC);

        if (result != 0) {
            if (errno == EMFILE) {
                LOG_ERROR("The process limit on the number of open file descriptors has been reached.");
            }
            else if (errno == ENFILE) {
                LOG_ERROR("The system wide limit on the number of open files has been reached.");
            }
        }

        return result;
    }

    bool setNonblock(const int fd)
    {
        int flags;

        flags = fcntl(fd, F_GETFL);
        if (flags < 0) {
            LOG_DEBUG("fcntl getfl failed");
            return false;
        }

        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            LOG_DEBUG("fcntl setfl failed");
            return false;
        }

        return true;
    }

    bool setBlocking(const int fd)
    {
        int flags;

        flags = fcntl(fd, F_GETFL);
        if (flags < 0) {
            LOG_DEBUG("fcntl getfl failed");
            return false;
        }

        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
            LOG_DEBUG("fcntl setfl failed");
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
                LOG_DEBUG("write failed");
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
                LOG_DEBUG("read failed");
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
                LOG_DEBUG("skip failed");
                return false;
            }
            pos += bytes;
        }

        return true;
    }
}
