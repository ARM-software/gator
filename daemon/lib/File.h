/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FILE_H
#define INCLUDE_LIB_FILE_H

#include <cstdio>

namespace lib {
    FILE * fopen_cloexec(const char * path, const char * mode);
}

#endif // INCLUDE_LIB_FILE_H
