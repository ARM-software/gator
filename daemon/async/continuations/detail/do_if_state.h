/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_of.h"
#include "async/continuations/detail/then_state.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** Receives the values from the predicate post processor and conditionally starts either the then or else operation as appropriate */
    template<typename ThenOp, typename ElseOp, typename NextInitiator, typename... InputArgs>
    struct do_if_predicate_next_wrapper_t {
        using then_type = std::decay_t<ThenOp>;
        using else_type = std::decay_t<ElseOp>;
        using next_type = std::decay_t<NextInitiator>;
        using then_then_helper_type = typename then_helper_t<then_type, InputArgs...>::initiator_helper_type;
        using else_then_helper_type = typename then_helper_t<else_type, InputArgs...>::initiator_helper_type;

        lib::source_loc_t sloc;
        then_type then_op;
        else_type else_op;
        next_type next;

        template<typename... Args>
        explicit constexpr do_if_predicate_next_wrapper_t(lib::source_loc_t const & sloc,
                                                          then_type && then_op,
                                                          else_type && else_op,
                                                          Args &&... args)
            : sloc(sloc), then_op(std::move(then_op)), else_op(std::move(else_op)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, bool condition, InputArgs &&... args)
        {
            if (condition) {
                TRACE_CONTINUATION(sloc, "do_if_else... calling then");

                then_then_helper_type::initiate(sloc,
                                                std::move(then_op),
                                                std::move(next),
                                                exceptionally,
                                                std::move(args)...);
            }
            else {
                TRACE_CONTINUATION(sloc, "do_if_else... calling else");

                else_then_helper_type::initiate(sloc,
                                                std::move(else_op),
                                                std::move(next),
                                                exceptionally,
                                                std::move(args)...);
            }
        }
    };

    /** Post processes the predicate output, injecting the received arguments from the previous step, and passing them to the receiver */
    template<typename... InputArgs>
    struct do_if_predicate_post_processor_t {
        std::tuple<InputArgs...> args_tuple;

        explicit constexpr do_if_predicate_post_processor_t(InputArgs &&... args)
            : args_tuple(std::make_tuple(std::move(args)...))
        {
        }

        template<typename ThenOp, typename ElseOp, typename NextInitiator, typename Exceptionally>
        void operator()(do_if_predicate_next_wrapper_t<ThenOp, ElseOp, NextInitiator, InputArgs...> && next,
                        Exceptionally const & exceptionally,
                        bool condition)
        {
            std::apply([&next, &exceptionally, condition](
                           auto &&... args) mutable { next(exceptionally, condition, std::move(args)...); },
                       std::move(args_tuple));
        }
    };

    /** The continuation chain state object for do_if */
    template<typename Predicate, typename ThenOp, typename ElseOp, typename... InputArgs>
    struct do_if_state_t {
        /** Initiator object for do_if */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = do_if_state_t<Predicate, ThenOp, ElseOp, InputArgs...>;
            using next_type = do_if_predicate_next_wrapper_t<ThenOp, ElseOp, std::decay_t<NextInitiator>, InputArgs...>;
            using predicate_type = std::decay_t<Predicate>;
            using predicate_then_helper_type = typename then_helper_t<predicate_type>::initiator_helper_type;

            lib::source_loc_t sloc;
            predicate_type predicate;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : sloc(state.sloc),
                  predicate(std::move(state.predicate)),
                  next(state.sloc, std::move(state.then_op), std::move(state.else_op), std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
            {
                TRACE_CONTINUATION(sloc, "do_if_else... calling predicate");

                predicate_then_helper_type::initiate_with_post_process(
                    sloc,
                    do_if_predicate_post_processor_t<InputArgs...> {std::forward<InputArgs>(args)...},
                    std::move(predicate),
                    std::move(next),
                    exceptionally);
            }
        };

        lib::source_loc_t sloc;
        Predicate predicate;
        ThenOp then_op;
        ElseOp else_op;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"do_if_else", sloc}; }
    };
}
