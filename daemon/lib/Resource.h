/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <sys/resource.h>

namespace lib {
    /**
     * A wrapper around getrlimit that enables unit testing.
     */
    int getrlimit(int resource, rlimit * rlp);

    /**
     * A wrapper around setrlimit that enables unit testing.
     */
    int setrlimit(int resource, const struct rlimit * rlp);
}
