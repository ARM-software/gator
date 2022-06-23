/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_traits.h"
#include "async/continuations/detail/then_state.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>
#include <variant>

namespace async::continuations::detail {

    /** Helper type trait that extracts the return type from the variant operation */
    template<typename Op, typename Variant>
    using variant_op_return_type_t = std::decay_t<std::invoke_result_t<std::decay_t<Op>, std::decay_t<Variant>>>;

    /** Helper type trait that extracts the common return type (and therefore the next continuation's argument(s) */
    template<typename Op, typename... Variants>
    struct variant_op_common_return_type_t {
        using type = typename continuation_of_common_type_t<
            typename as_continuation_args_t<variant_op_return_type_t<Op, Variants>>::type...>::type;
    };

    /** Receiver of the unpacked value, passes it the the operation */
    template<typename Op, typename NextInitiator>
    struct unpack_variant_initiator_wrapper_t {
        lib::source_loc_t sloc;
        Op op;
        NextInitiator next;

        template<typename... Args>
        constexpr unpack_variant_initiator_wrapper_t(lib::source_loc_t const & sloc, Op && op, Args &&... args)
            : sloc(sloc), op(std::move(op)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally, typename T>
        void operator()(Exceptionally const & exceptionally, T && value)
        {
            TRACE_CONTINUATION(sloc, "unpack_variant");

            using arg_type = std::decay_t<T>;

            static_assert(std::is_invocable_v<Op, arg_type>,
                          "then operation does not correctly consume preceeding operations output");

            using initiator_helper_type = typename then_helper_t<Op, arg_type>::initiator_helper_type;

            initiator_helper_type::initiate(sloc,
                                            std::move(op),
                                            std::move(next),
                                            exceptionally,
                                            std::forward<T>(value));
        }
    };

    /** The continuation chain state object for unpack_variant */
    template<typename Op, typename... FromVariants>
    struct unpack_variant_state_t {
        /** Initiator object for unpack_variant */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = unpack_variant_state_t<Op, FromVariants...>;
            using next_type = unpack_variant_initiator_wrapper_t<std::decay_t<Op>, std::decay_t<NextInitiator>>;

            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : next(state.sloc, std::move(state.op), std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, std::variant<FromVariants...> && variant)
            {
                std::visit([this, &exceptionally](
                               auto && value) { next(exceptionally, std::forward<decltype(value)>(value)); },
                           std::move(variant));
            }
        };

        lib::source_loc_t sloc;
        Op op;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"unpack_variant", sloc}; }
    };
}
