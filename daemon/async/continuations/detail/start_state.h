/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** The continuation chain state object for start_with */
    template<typename... Values>
    struct start_with_state_t {
        static_assert(!std::disjunction_v<std::is_reference<Values>...>, "Argument types must be value types");

        /** Initiator object for start_with */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = start_with_state_t<Values...>;
            using next_type = std::decay_t<NextInitiator>;

            state_type state;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : state(std::move(state)), next(std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally)
            {
                std::apply([this, &exceptionally](auto &&... v) mutable { this->next(exceptionally, std::move(v)...); },
                           std::move(state.values_tuple));
            }
        };

        std::tuple<Values...> values_tuple;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"start_with", {}}; }
    };

    /** The continuation chain state object for start_by */
    template<typename Op>
    struct start_by_state_t {
        /** Initiator object for start_by */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = start_by_state_t<Op>;
            using next_type = std::decay_t<NextInitiator>;

            state_type state;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : state(std::move(state)), next(std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally)
            {
                TRACE_CONTINUATION(state.sloc, "start_by");

                next(exceptionally, state.op());
            }
        };

        lib::source_loc_t sloc;
        Op op;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"start_by", sloc}; }
    };
}
