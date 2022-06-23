/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/asio_traits.h"
#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/on_executor_state.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {

    /**
     * Factory for constructing a continuation_t for the 'on_executor' operation
     *
     * @tparam Mode The executor mode
     * @tparam Executor The executor type
     * @tparam Args The continuation argument types
     */
    template<on_executor_mode_t Mode, typename Executor, typename... Args>
    class on_executor_factory_t {
    public:
        static_assert(is_asio_executor_v<Executor>);
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        /** Create the 'on_executor' continuation */
        template<typename FromState>
        static constexpr auto make_continuation(continuation_t<FromState, Args...> && from,
                                                lib::source_loc_t const & sloc,
                                                Executor const & ex)
        {
            using next_factory = continuation_factory_t<Args...>;
            using state_type = on_executor_state_t<Mode, std::decay_t<Executor>, Args...>;

            return next_factory::make_continuation(std::move(from), state_type {sloc, ex});
        }
    };
}
