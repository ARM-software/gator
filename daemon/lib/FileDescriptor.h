/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FILE_DESCRIPTOR_H
#define INCLUDE_LIB_FILE_DESCRIPTOR_H

namespace lib
{
    int pipe_cloexec(int pipefd[2]);
    bool setNonblock(const int fd);
    bool writeAll(const int fd, const void * const buf, const size_t pos);
    bool readAll(const int fd, void * const buf, const size_t count);
    bool skipAll(const int fd, const size_t count);
}

#endif // INCLUDE_LIB_FILE_DESCRIPTOR_H

