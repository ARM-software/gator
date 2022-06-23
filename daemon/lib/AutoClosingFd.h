/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_AUTO_CLOSING_FD_H
#define INCLUDE_LIB_AUTO_CLOSING_FD_H

#include "lib/Syscall.h"

#include <utility>

#include <unistd.h>

namespace lib {
    /**
     * Holds a file descriptor int value, and will autoclose it on destruction
     */
    class AutoClosingFd {
    public:
        /** Constructor, invalid fd */
        constexpr AutoClosingFd() : fd(-1) {}
        /** Constructor, take ownership of fd */
        explicit constexpr AutoClosingFd(int fd) : fd(fd) {}
        /** Constructor, move operation */
        AutoClosingFd(AutoClosingFd && that) noexcept : fd(-1) { std::swap(fd, that.fd); }
        /** Move assignment */
        inline AutoClosingFd & operator=(AutoClosingFd && that) noexcept
        {
            AutoClosingFd destroyable;
            // destroyable takes ownership of fd, that gets -1
            std::swap(destroyable.fd, that.fd);
            // this takes ownership of fd from destroyable, destroyable gets old fd of this which will be closed
            std::swap(this->fd, destroyable.fd);
            return *this;
        }
        /** Copy is not valid */
        AutoClosingFd(const AutoClosingFd &) = delete;
        AutoClosingFd & operator=(const AutoClosingFd &) = delete;

        /** Destructor */
        ~AutoClosingFd() { close(); }

        /** Explicitly close the fd */
        void close()
        {
            int fd = -1;
            std::swap(this->fd, fd);
            if (fd != -1) {
                lib::close(fd);
            }
        }

        /** Take ownership of new fd */
        void reset(int fd)
        {
            AutoClosingFd that {fd};
            std::swap(this->fd, that.fd);
        }

        /** Take ownership of new fd */
        AutoClosingFd & operator=(int fd)
        {
            reset(fd);
            return *this;
        }

        /** Release ownership of the fd */
        [[nodiscard]] int release()
        {
            int fd = -1;
            std::swap(this->fd, fd);
            return fd;
        }

        /** Get the fd value */
        [[nodiscard]] int operator*() const { return fd; }

        /** Get the fd value */
        [[nodiscard]] int get() const { return fd; }

        /** Indicates contains valid fd (where valid means not -1) */
        [[nodiscard]] explicit operator bool() const { return fd != -1; }

    private:
        int fd;
    };
}

#endif /* INCLUDE_LIB_AUTO_CLOSING_FD_H */
