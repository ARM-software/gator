/* Copyright (C) 2016-2023 by Arm Limited. All rights reserved. */

#include "lib/Assert.h"

#include "Config.h"

#if CONFIG_ASSERTIONS

#include <cstdio>
#include <string>

#if defined(GATOR_UNIT_TESTS) && GATOR_UNIT_TESTS
#include <stdexcept>
#else
#include <cstdlib>
#endif

namespace lib::_assert_internal {
    /**
         * Assertion helper; outputs error message and terminates
         */
    void runtime_assert_terminate(const char * file, unsigned line, const char * func, const std::string & msg)
    {
        fprintf(stderr, "Assertion failed failure in '%s' @ [%s:%u]: %s\n", func, file, line, msg.c_str());

#if defined(GATOR_UNIT_TESTS) && GATOR_UNIT_TESTS
        throw std::runtime_error("failed in runtime_assert");
#else
        abort();
#endif
    }
}

#endif /* CONFIG_ASSERTIONS */
