/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation.h"
#include "async/continuations/detail/polymorphic_state.h"
#include "async/continuations/detail/state_chain.h"

#include <tuple>
#include <utility>

namespace async::continuations::detail {
    /**
     * A helper object that is used to construct continuation_t objects
     *
     * @tparam Args The input arguments that the continuation will pass to the receiver
     */
    template<typename... Args>
    struct continuation_factory_t {
        /** Append a new state to a chain of continuation operations */
        template<typename T, typename... Tail, typename... From, typename U>
        static constexpr auto make_continuation(continuation_t<state_chain_t<T, Tail...>, From...> && from,
                                                U && new_tail)
        {
            using state_chain_type = state_chain_t<T, Tail..., std::decay_t<U>>;

            return continuation_t<state_chain_type, Args...>(
                state_chain_type {std::move(from.state_chain), std::forward<U>(new_tail)});
        }

        /** Append a new state to a chain of continuation operations */
        template<typename... From, typename U>
        static constexpr auto make_continuation(continuation_t<polymorphic_state_t<From...>, From...> && from,
                                                U && new_tail)
        {
            using state_chain_type = state_chain_t<polymorphic_state_t<From...>, std::decay_t<U>>;

            return continuation_t<state_chain_type, Args...>(
                state_chain_type {std::move(from.state_chain), std::forward<U>(new_tail)});
        }

        /** Create a new continuation chain */
        template<typename T>
        static constexpr auto make_continuation(T && state)
        {
            using state_chain_type = state_chain_t<std::decay_t<T>>;

            return continuation_t<state_chain_type, Args...>(state_chain_type {std::forward<T>(state)});
        }
    };
}
