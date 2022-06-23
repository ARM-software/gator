/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** The continuation chain state object for cont_if */
    template<bool Expected, typename... InputArgs>
    struct cont_if_state_t {
        /** The initiator type template */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = cont_if_state_t<Expected, InputArgs...>;
            using next_type = std::decay_t<NextInitiator>;

            state_type state;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : state(std::move(state)), next(std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            constexpr void operator()(Exceptionally const & exceptionally, bool cond, InputArgs &&... values)
            {
                TRACE_CONTINUATION(state.sloc,
                                   "cont_if<%s>{ cond=%s }",
                                   (Expected ? "true" : "false"),
                                   (cond ? "true" : "false"));

                if (cond == Expected) {
                    next(exceptionally, std::move(values)...);
                }
            }
        };

        lib::source_loc_t sloc;

        [[nodiscard]] constexpr name_and_loc_t trace() const
        {
            if constexpr (Expected) {
                return {"cont_if<true>", sloc};
            }
            else {
                return {"cont_if<false>", sloc};
            }
        }
    };
}
