/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FILE_DESCRIPTOR_H
#define INCLUDE_LIB_FILE_DESCRIPTOR_H

namespace lib {
    int pipe_cloexec(int pipefd[2]);
    bool setNonblock(int fd);
    bool writeAll(int fd, const void * buf, size_t pos);
    bool readAll(int fd, void * buf, size_t count);
    bool skipAll(int fd, size_t count);
}

#endif // INCLUDE_LIB_FILE_DESCRIPTOR_H
