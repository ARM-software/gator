/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/unpack_tuple_state.h"
#include "lib/source_location.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {

    /**
     * Factory for constructing a continuation_t for the 'unpack tuple' operation
     */
    struct unpack_tuple_factory_t {

        /** Create the continuation */
        template<typename FromState, typename A, typename B>
        static constexpr auto make_continuation(continuation_t<FromState, std::pair<A, B>> && from,
                                                lib::source_loc_t const & sloc)
        {
            using next_factory = continuation_factory_t<A, B>;
            using state_type = unpack_tuple_state_t<std::pair<A, B>>;

            return next_factory::make_continuation(std::move(from), state_type {sloc});
        }

        /** Create the continuation */
        template<typename FromState, typename... Ts>
        static constexpr auto make_continuation(continuation_t<FromState, std::tuple<Ts...>> && from,
                                                lib::source_loc_t const & sloc)
        {
            using next_factory = continuation_factory_t<Ts...>;
            using state_type = unpack_tuple_state_t<std::tuple<Ts...>>;

            return next_factory::make_continuation(std::move(from), state_type {sloc});
        }
    };
}
