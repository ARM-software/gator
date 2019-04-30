/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_STRING_H
#define INCLUDE_LIB_STRING_H

#include <string.h>

namespace lib
{
    /**
     * Like strdup but returns nullptr if the input is null
     * @param s
     * @return
     */
    static char * strdup_null(const char * s)
    {
        if (s == nullptr)
            return nullptr;
        return ::strdup(s);
    }
}

#endif // INCLUDE_LIB_STRING_H

