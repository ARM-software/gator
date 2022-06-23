/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/predicate_state.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /**
     * Factory for constructing a continuation_t for the 'predicate' operation
     *
     * @tparam Predicate the predicate operation
     */
    template<bool Expected, typename Predicate>
    struct predicate_factory_t {
        /** Create the 'predicate' continuation */
        template<typename FromState, typename... FromArgs>
        static constexpr auto make_continuation(continuation_t<FromState, FromArgs...> && from,
                                                lib::source_loc_t const & sloc,
                                                Predicate && predicate)
        {
            using next_factory = continuation_factory_t<>;
            using state_type = predicate_state_t<Expected, std::decay_t<Predicate>, FromArgs...>;

            return next_factory::make_continuation(std::move(from),
                                                   state_type {sloc, std::forward<Predicate>(predicate)});
        }
    };
}
