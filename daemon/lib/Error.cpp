/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#include "Error.h"

#include <cerrno>
#include <cstring>

namespace lib {

    const char * strerror(int err_no)
    {
        /*
         * Assume strerror is thread-safe - as it is in glibc and musl
         *
         * Ideally this would be conditional, although musl doesn't seem to provide a simple way to detect it
        */
        return ::strerror(err_no); // NOLINT(concurrency-mt-unsafe)
    }

    const char * strerror()
    {
        return strerror(errno);
    }

}
