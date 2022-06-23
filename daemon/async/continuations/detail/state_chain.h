/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /**
     * Constructs a chain of nested types, such that for each subsequent type in the argument list, will form an increasingly nested inner structure.
     *
     * The state chain represents the sequence of operations to be performed as part of the continuation. The first (outer-most element represents 
     * the start of the chain)
     *
     * For example state_chain_t<type_a_t, type_b_t, type_c_t> will look something like:
     *
     * { type_a_t value; { type_b_t value; { type_c_t value; } } }
     *
     * @tparam T The value type
     * @tparam Tail The nested sequence of value types
     */
    template<typename T, typename... Tail>
    struct state_chain_t {
        using value_type = std::decay_t<T>;
        using next_type = state_chain_t<Tail...>;

        value_type value;
        next_type next;

        /** Constructor, for appending */
        template<typename... Args, typename U>
        constexpr state_chain_t(state_chain_t<T, Args...> && head, U && tail) noexcept
            : value(std::move(head.value)), next(std::move(head.next), std::forward<U>(tail))
        {
        }
    };

    /** Speciailization for last two items in the chain */
    template<typename T, typename U>
    struct state_chain_t<T, U> {
        using value_type = std::decay_t<T>;
        using next_type = state_chain_t<U>;

        value_type value;
        next_type next;

        /** Constructor, for appending */
        constexpr state_chain_t(state_chain_t<T> && head, U && tail) noexcept
            : value(std::move(head.value)), next(std::move(tail))
        {
        }

        /** Constructor, for appending */
        constexpr state_chain_t(T && head, U && tail) noexcept : value(std::forward<T>(head)), next(std::move(tail)) {}
    };

    /** Speciailization for last item in the chain */
    template<typename T>
    struct state_chain_t<T> {
        using value_type = std::decay_t<T>;

        value_type value;

        /** Constructor */
        explicit constexpr state_chain_t(T && value) noexcept : value(std::move(value)) {}
    };
}
