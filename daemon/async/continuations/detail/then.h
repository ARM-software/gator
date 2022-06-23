/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/then_state.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /**
     * Factory for constructing a continuation_t for the 'then' operation
     */
    struct then_factory_t {

        /** Create the 'then' continuation */
        template<typename FromState, typename... FromArgs, typename Op>
        static constexpr auto make_continuation(continuation_t<FromState, FromArgs...> && from,
                                                lib::source_loc_t const & sloc,
                                                Op && op)
        {
            using op_type = std::decay_t<Op>;
            using result_type = std::decay_t<std::invoke_result_t<op_type, std::decay_t<FromArgs>...>>;

            static_assert(!std::is_reference_v<result_type>, "result_type type must be value types");

            using helper_type = then_helper_t<op_type, FromArgs...>;
            using next_factory = typename helper_type::next_factory;
            using state_type = then_state_t<std::decay_t<Op>, FromArgs...>;

            return next_factory::make_continuation(std::move(from), state_type {sloc, std::forward<Op>(op)});
        }
    };
}
