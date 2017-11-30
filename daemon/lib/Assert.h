/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_ASSERT_H
#define INCLUDE_LIB_ASSERT_H

#if !defined(NDEBUG)

#include <cstdlib>
#include <string>


namespace lib
{
    namespace _assert_internal
    {
        /**
         * Terminate after logging some message
         */
        extern void runtime_assert_terminate(const char * file, unsigned line, const char * func, const std::string & msg);
    }

/* For unit tests... */
#if !defined(__PRETTY_FUNCTION__)
# if defined(__FUNCSIG__)
#   define __PRETTY_FUNCTION__ __FUNCSIG__
# else
#   define __PRETTY_FUNCTION__ __FUNCTION__
# endif
#endif

/** assertion macro */
#define     runtime_assert(test, msg)                                               \
    do {                                                                            \
        if (!(test)) {                                                              \
            ::lib::_assert_internal::runtime_assert_terminate(__FILE__,             \
                                                              __LINE__,             \
                                                              __PRETTY_FUNCTION__,  \
                                                              msg);                 \
        }                                                                           \
    } while (0)
}

#else /* !defined(NDEBUG) */

/** assertion macro */
#define     runtime_assert(test, msg)    do { if (!(test)) { } } while (0)

#endif /* !defined(NDEBUG) */

#endif /* INCLUDE_LIB_ASSERT_H */
