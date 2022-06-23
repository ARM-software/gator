/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/do_if_state.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /**
     * Factory for constructing a continuation_t for the 'do_if' operation
     *
     * @tparam Predicate The predicate function
     * @tparam ThenOp The then operation
     * @tparam ElseOp The else operation
     * @tparam Args The continuation arguments (which are forwarded from the previous to the next step)
     */
    template<typename Predicate, typename ThenOp, typename ElseOp, typename... Args>
    class do_if_factory_t {
    public:
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        /** Create the 'do_if' continuation */
        template<typename FromState>
        static constexpr auto make_continuation(continuation_t<FromState, Args...> && from,
                                                lib::source_loc_t const & sloc,
                                                Predicate && predicate,
                                                ThenOp && then_op,
                                                ElseOp && else_op)
        {
            using predicate_type = std::decay_t<Predicate>;
            using then_type = std::decay_t<ThenOp>;
            using else_type = std::decay_t<ElseOp>;

            static_assert(std::is_invocable_v<predicate_type>, "predicate operation is not callable");
            static_assert(std::is_invocable_v<then_type, Args...>, "then operation is not callable");
            static_assert(std::is_invocable_v<else_type, Args...>, "then operation is not callable");

            using predicate_result_type =
                typename as_continuation_args_t<std::decay_t<std::invoke_result_t<predicate_type>>>::type;
            using then_result_type =
                typename as_continuation_args_t<std::decay_t<std::invoke_result_t<then_type, Args...>>>::type;
            using else_result_type =
                typename as_continuation_args_t<std::decay_t<std::invoke_result_t<else_type, Args...>>>::type;
            using next_result_type = typename continuation_of_common_type_t<then_result_type, else_result_type>::type;

            static_assert(std::is_same_v<continuation_of_t<bool>, predicate_result_type>,
                          "do_if requires a predicate that returns bool or a continuation thereof");

            using helper_type = helper_t<next_result_type>;

            return helper_type::make_continuation(std::move(from),
                                                  sloc,
                                                  std::forward<Predicate>(predicate),
                                                  std::forward<ThenOp>(then_op),
                                                  std::forward<ElseOp>(else_op));
        }

    private:
        template<typename T>
        class helper_t;

        template<typename... NextArgs>
        class helper_t<continuation_of_t<NextArgs...>> {
        public:
            template<typename FromState>
            static constexpr auto make_continuation(continuation_t<FromState, Args...> && from,
                                                    lib::source_loc_t const & sloc,
                                                    Predicate && predicate,
                                                    ThenOp && then_op,
                                                    ElseOp && else_op)
            {
                using next_factory = continuation_factory_t<NextArgs...>;
                using state_type =
                    do_if_state_t<std::decay_t<Predicate>, std::decay_t<ThenOp>, std::decay_t<ElseOp>, Args...>;

                return next_factory::make_continuation(std::move(from),
                                                       state_type {
                                                           sloc,
                                                           std::forward<Predicate>(predicate),
                                                           std::forward<ThenOp>(then_op),
                                                           std::forward<ElseOp>(else_op),
                                                       });
            }
        };
    };
}
