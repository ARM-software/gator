/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#include "lib/File.h"

#include <fcntl.h>

namespace lib {
    FILE * fopen_cloexec(const char * path, const char * mode)
    {
        FILE * fh = fopen(path, mode);
        if (fh == nullptr) {
            return nullptr;
        }
        int fd = fileno(fh);
        int fdf = fcntl(fd, F_GETFD);
        if ((fdf == -1) || (fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
            fclose(fh);
            return nullptr;
        }
        return fh;
    }

}
