/* Copyright (C) 2016-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_ASSERT_H
#define INCLUDE_LIB_ASSERT_H

#include "Config.h"

#if CONFIG_ASSERTIONS

#include <cstdlib>
#include <string>

namespace lib {
    namespace _assert_internal {
        /**
         * Terminate after logging some message
         */
        extern void runtime_assert_terminate(const char * file,
                                             unsigned line,
                                             const char * func,
                                             const std::string & msg);
    }

/* For unit tests... */
#if !defined(__PRETTY_FUNCTION__)
#if defined(__FUNCSIG__)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#else
#define __PRETTY_FUNCTION__ __FUNCTION__ // NOLINT(bugprone-reserved-identifier)
#endif
#endif

/** assertion macro */
#define runtime_assert(test, msg)                                                                                      \
    do {                                                                                                               \
        if (!(test)) { /* NOLINT(readability-simplify-boolean-expr) */                                                 \
            ::lib::_assert_internal::runtime_assert_terminate(__FILE__, __LINE__, __PRETTY_FUNCTION__, msg);           \
        }                                                                                                              \
    } while (0)
}

#else /* CONFIG_ASSERTIONS */

/** assertion macro */
#define runtime_assert(test, msg)                                                                                      \
    do {                                                                                                               \
        if (!(test)) {                                                                                                 \
        }                                                                                                              \
    } while (0)

#endif /* CONFIG_ASSERTIONS */

#endif /* INCLUDE_LIB_ASSERT_H */
