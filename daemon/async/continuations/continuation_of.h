/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

namespace async::continuations {
    /** A helper template that is used to wrap the variadic argument types for some continuation */
    template<typename... Args>
    struct continuation_of_t {};
}
