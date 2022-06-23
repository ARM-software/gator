/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/then_state.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** The continuation chain state object for loop */
    template<typename Predicate, typename Generator, typename... InputArgs>
    struct loop_state_t {
        /** Initiator object for loop */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = loop_state_t<Predicate, Generator, InputArgs...>;
            using next_type = std::decay_t<NextInitiator>;
            using predicate_type = std::decay_t<Predicate>;
            using generator_type = std::decay_t<Generator>;
            using predicate_then_helper_type =
                typename then_helper_t<predicate_type &, InputArgs...>::initiator_helper_type;
            using generator_then_helper_type =
                typename then_helper_t<generator_type &, InputArgs...>::initiator_helper_type;

            /** The shared state for each loop iteration */
            struct iteration_state_t {
                state_type state;
                next_type next;
                std::size_t loop_count = 0;

                template<typename... Args>
                explicit constexpr iteration_state_t(state_type && state, Args &&... args)
                    : state(std::move(state)), next(std::forward<Args>(args)...)
                {
                }
            };

            /** Initiator that consumes the output of the generator and starts the next predicate */
            struct generator_result_initiator_t {
                std::unique_ptr<iteration_state_t> iteration_state;

                template<typename Exceptionally>
                void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
                {
                    auto const & sloc = iteration_state->state.sloc;

                    predicate_type & predicate = iteration_state->state.predicate;

                    TRACE_CONTINUATION(sloc,
                                       "loop... calling predicate (iteration=%zu)",
                                       ++(iteration_state->loop_count));

                    predicate_then_helper_type::initiate(sloc,
                                                         predicate,
                                                         predicate_result_initiator_t {std::move(iteration_state)},
                                                         exceptionally,
                                                         std::move(args)...);
                }
            };

            /** Initiator that consumes the output of the predicate and starts the next generator or completes the loop */
            struct predicate_result_initiator_t {
                std::unique_ptr<iteration_state_t> iteration_state;

                template<typename Exceptionally>
                void operator()(Exceptionally const & exceptionally, bool condition, InputArgs &&... args)
                {
                    auto const & sloc = iteration_state->state.sloc;

                    if (condition) {
                        generator_type & generator = iteration_state->state.generator;

                        TRACE_CONTINUATION(sloc,
                                           "loop... calling generator (iteration=%zu)",
                                           iteration_state->loop_count);

                        generator_then_helper_type::initiate(sloc,
                                                             generator,
                                                             generator_result_initiator_t {std::move(iteration_state)},
                                                             exceptionally,
                                                             std::move(args)...);
                    }
                    else {
                        TRACE_CONTINUATION(sloc, "loop... complete (iteration=%zu)", iteration_state->loop_count);

                        iteration_state->next(exceptionally, std::move(args)...);
                    }
                }
            };

            // hold the common state on the heap to avoid repeated copies on the stack
            std::unique_ptr<iteration_state_t> iteration_state;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : iteration_state(std::make_unique<iteration_state_t>(std::move(state), std::forward<Args>(args)...))
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
            {
                auto const & sloc = iteration_state->state.sloc;

                TRACE_CONTINUATION(sloc, "loop... calling predicate (iteration=%zu)", iteration_state->loop_count);

                predicate_type & predicate = iteration_state->state.predicate;

                predicate_then_helper_type::initiate(sloc,
                                                     predicate,
                                                     predicate_result_initiator_t {std::move(iteration_state)},
                                                     exceptionally,
                                                     std::move(args)...);
            }
        };

        lib::source_loc_t sloc;
        Predicate predicate;
        Generator generator;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"loop", sloc}; }
    };
}
