/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_AUTO_CLOSING_FD_H
#define INCLUDE_LIB_AUTO_CLOSING_FD_H

#include <unistd.h>
#include <utility>

#include "lib/Syscall.h"

namespace lib
{
    /**
     * Holds a file descriptor int value, and will autoclose it on destruction
     */
    class AutoClosingFd
    {
    public:

        /** Constructor, invalid fd */
        inline AutoClosingFd() : fd(-1) {}
        /** Constructor, take ownership of fd */
        inline AutoClosingFd(int fd) : fd(fd) {}
        /** Constructor, move operation */
        inline AutoClosingFd(AutoClosingFd && that) : fd(-1)
        {
            std::swap(fd, that.fd);
        }
        /** Move assignment */
        inline AutoClosingFd & operator = (AutoClosingFd && that)
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
        AutoClosingFd & operator = (const AutoClosingFd &) = delete;

        /** Destructor */
        inline ~AutoClosingFd()
        {
            int fd = -1;
            std::swap(this->fd, fd);
            if (fd != -1) {
                close(fd);
            }
        }

        /** Take ownership of new fd */
        inline void reset(int fd)
        {
            AutoClosingFd that {fd};
            std::swap(this->fd, that.fd);
        }

        /** Take ownership of new fd */
        inline AutoClosingFd & operator = (int fd)
        {
            reset(fd);
            return *this;
        }

        /** Release ownership of the fd */
        inline int release()
        {
            int fd = -1;
            std::swap(this->fd, fd);
            return fd;
        }

        /** Get the fd value */
        inline int operator * () const
        {
            return fd;
        }

        /** Get the fd value */
        inline int get() const
        {
            return fd;
        }

        /** Indicates contains valid fd (where valid means not -1) */
        inline operator bool() const
        {
            return fd != -1;
        }



    private:

        int fd;
    };
}

#endif /* INCLUDE_LIB_AUTO_CLOSING_FD_H */
