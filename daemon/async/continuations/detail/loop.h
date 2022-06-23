/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_of.h"
#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/loop_state.h"
#include "lib/source_location.h"

namespace async::continuations::detail {
    /**
     * Factory for constructing a continuation_t for the 'loop' operation
     */
    struct loop_factory_t {
        template<typename FromState, typename... Args, typename Predicate, typename Generator>
        static constexpr auto make_continuation(continuation_t<FromState, Args...> && from,
                                                lib::source_loc_t const & sloc,
                                                Predicate && predicate,
                                                Generator && generator)
        {
            using predicate_type = std::decay_t<Predicate>;
            using generator_type = std::decay_t<Generator>;

            // Predicate validation
            static_assert(std::is_invocable_v<predicate_type, Args...>, "predicate operation is not callable");
            using predicate_result_type =
                typename as_continuation_args_t<std::decay_t<std::invoke_result_t<predicate_type, Args...>>>::type;
            static_assert(std::is_same_v<continuation_of_t<bool, Args...>, predicate_result_type>,
                          "loop requires a predicate that returns a continuation with the first element a bool");

            // Generator validation
            static_assert(std::is_invocable_v<generator_type, Args...>, "generator operation is not callable");
            using generator_result_type =
                typename as_continuation_args_t<std::decay_t<std::invoke_result_t<generator_type, Args...>>>::type;
            static_assert(
                std::is_same_v<continuation_of_t<Args...>, generator_result_type>,
                "loop requires a generator that returns an updated value, or a continuation of updated values");

            using factory_type = continuation_factory_t<Args...>;
            using state_type = loop_state_t<predicate_type, generator_type, Args...>;

            return factory_type::make_continuation(
                std::move(from),
                state_type {sloc, std::forward<Predicate>(predicate), std::forward<Generator>(generator)});
        }
    };
}
