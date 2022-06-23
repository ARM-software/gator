/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_traits.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <tuple>
#include <utility>

namespace async::continuations::detail {
    /** The continuation chain state object for unpack_tuple */
    template<typename TupleType>
    struct unpack_tuple_state_t {
        /** Initiator object for unpack_variant */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = unpack_tuple_state_t<TupleType>;
            using next_type = std::decay_t<NextInitiator>;

            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && /*state*/, Args &&... args)
                : next(std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, TupleType && tuple)
            {
                std::apply([this, &exceptionally](
                               auto &&... values) { next(exceptionally, std::forward<decltype(values)>(values)...); },
                           std::move(tuple));
            }
        };

        lib::source_loc_t sloc;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"unpack_tuple", sloc}; }
    };

}
