/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/then_state.h"
#include "async/continuations/detail/trace.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** The continuation chain state object for predicate */
    template<bool Expected, typename Predicate, typename... InputArgs>
    struct predicate_state_t {
        /** Initiator object for predicate */
        template<typename NextInitiator>
        struct initiator_type {
            /** Post-process the received result from the predicate, conditionally executing the next item in the chain */
            struct predicate_post_processor_t {
                lib::source_loc_t sloc;

                template<typename Next, typename Exceptionally>
                void operator()(Next && next, Exceptionally const & exceptionally, bool condition)
                {
                    TRACE_CONTINUATION(sloc,
                                       "predicate<%s>... received condition=%s",
                                       (Expected ? "true" : "false"),
                                       (condition ? "true" : "false"));

                    if (condition == Expected) {
                        next(exceptionally);
                    }
                }
            };

            using state_type = predicate_state_t<Expected, Predicate, InputArgs...>;
            using predicate_type = std::decay_t<Predicate>;
            using predicate_then_helper_type = then_helper_t<predicate_type, InputArgs...>;
            using next_type = typename predicate_then_helper_type::
                template initiator_with_post_process_type<predicate_post_processor_t, std::decay_t<NextInitiator>>;

            lib::source_loc_t sloc;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : sloc(state.sloc),
                  next(state.sloc,
                       std::move(state.predicate),
                       predicate_post_processor_t {state.sloc},
                       std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
            {
                TRACE_CONTINUATION(sloc, "predicate<%s>... calling predicate", (Expected ? "true" : "false"));

                next(exceptionally, std::move(args)...);
            }
        };

        lib::source_loc_t sloc;
        Predicate predicate;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"predicate", sloc}; }
    };
}
