/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include <exception>

namespace async::continuations {
    /** The null receiver discards the results / exceptions, terminating a sequence of operations */
    template<typename... Args>
    struct nop_result_receiver_t {
        void operator()(Args... /*args*/) noexcept {}
    };

    struct nop_exception_receiver_t {
        void operator()(const std::exception_ptr & /*ex*/) noexcept {}
    };
}
