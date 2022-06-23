/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/cont_if_state.h"
#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/state_chain.h"
#include "lib/source_location.h"

namespace async::continuations::detail {
    /**
     * Factory for constructing a continuation_t for the 'continue_if_true' / 'continue_if_false' operations
     *
     * @tparam Expected The expected condition value
     * @tparam Args The continuation arguments (which are forwarded from the previous to the next step)
     */
    template<bool Expected, typename... Args>
    class cont_if_factory_t {
    public:
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        /** Create the 'cont_if' continuation */
        template<typename FromState>
        static constexpr auto make_continuation(continuation_t<FromState, bool, Args...> && from,
                                                lib::source_loc_t const & sloc)
        {
            using next_factory = continuation_factory_t<Args...>;
            using state_type = cont_if_state_t<Expected, Args...>;

            return next_factory::make_continuation(std::move(from), state_type {sloc});
        }
    };
}
