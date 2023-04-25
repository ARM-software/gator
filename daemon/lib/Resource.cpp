/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "Resource.h"

namespace lib {
    int getrlimit(int resource, rlimit * rlp)
    {
        return ::getrlimit(resource, rlp);
    }

    int setrlimit(int resource, const struct rlimit * rlp)
    {
        return ::setrlimit(resource, rlp);
    }
}
