/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <type_traits>

#include <async/continuations/continuation_of.h>

namespace async::continuations {
    //forward
    template<typename StateChain, typename... Args>
    struct continuation_t;

    /** Helper type trait for extracting the continuation args list from some type */
    template<typename ReturnType>
    struct as_continuation_args_t {
        using type = continuation_of_t<ReturnType>;
    };

    template<>
    struct as_continuation_args_t<void> {
        using type = continuation_of_t<>;
    };

    template<typename StateChain, typename... Args>
    struct as_continuation_args_t<continuation_t<StateChain, Args...>> {
        using type = continuation_of_t<Args...>;
    };

    /** Helper type trait that finds the common arg types for a set of continuations */
    template<typename... Args>
    struct continuation_of_common_type_t;

    // terminal or single list item, just provide the args as is
    template<typename... A>
    struct continuation_of_common_type_t<continuation_of_t<A...>> {
        using type = continuation_of_t<A...>;
    };

    // pair of items, find the common type of each argument
    template<typename... A, typename... B>
    struct continuation_of_common_type_t<continuation_of_t<A...>, continuation_of_t<B...>> {
        using type = continuation_of_t<std::common_type_t<A, B>...>;
    };

    // variadic sequence, repeatedly apply and find the common type
    template<typename... A, typename... B, typename... Other>
    struct continuation_of_common_type_t<continuation_of_t<A...>, continuation_of_t<B...>, Other...> {
        using pair_type = continuation_of_t<std::common_type_t<A, B>...>;
        using other_type = typename continuation_of_common_type_t<Other...>::type;
        using type = typename continuation_of_common_type_t<pair_type, other_type>::type;
    };
}
