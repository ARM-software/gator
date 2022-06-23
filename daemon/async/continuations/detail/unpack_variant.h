/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/unpack_variant_state.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>
#include <variant>

namespace async::continuations::detail {

    /**
     * Factory for constructing a continuation_t for the 'unpack variant' operation
     *
     * @tparam NextArgs The type list for the subsequent continuation (i.e. the values produced by the operation by this continuation)
     */
    template<typename... NextArgs>
    struct unpack_variant_factory_t {

        /** Create the continuation */
        template<typename FromState, typename... FromVariants, typename Op>
        static constexpr auto make_continuation(continuation_t<FromState, std::variant<FromVariants...>> && from,
                                                lib::source_loc_t const & sloc,
                                                Op && op)
        {
            using next_factory = continuation_factory_t<NextArgs...>;
            using state_type = unpack_variant_state_t<std::decay_t<Op>, FromVariants...>;

            return next_factory::make_continuation(std::move(from), state_type {sloc, std::forward<Op>(op)});
        }
    };

    /** Helper type trait that gives the factory type based on a continuation_of_t's arguments list */
    template<typename T>
    struct unpack_variant_factory_from_t;

    template<typename... NextArgs>
    struct unpack_variant_factory_from_t<continuation_of_t<NextArgs...>> {
        using type = unpack_variant_factory_t<NextArgs...>;
    };
}
