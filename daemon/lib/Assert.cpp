/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include    "lib/Assert.h"

#if !defined(NDEBUG)

#include    <cstdio>
#include    <cstdlib>

namespace lib
{
    namespace _assert_internal
    {
        /**
         * Assertion helper; outputs error message and terminates
         */
        void runtime_assert_terminate(const char * file, unsigned line, const char * func, const std::string & msg)
        {
            fprintf(stderr, "Assertion failed failure in '%s' @ [%s:%u]: %s\n", func, file, line, msg.c_str());
            abort();
        }
    }
}

#endif /* !defined(NDEBUG) */
