/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_of.h"
#include "async/continuations/continuation_traits.h"
#include "async/continuations/detail/initiation_chain.h"
#include "async/continuations/detail/polymorphic_state.h"
#include "async/continuations/detail/state_chain.h"
#include "async/continuations/detail/trace.h"
#include "async/continuations/nop_receiver.h"
#include "lib/source_location.h"

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/async_result.hpp>

namespace async::continuations {
    /**
     * A continuation represents some callable object that will initiate a chain of one or more operations.
     *
     * @tparam StateChain A state_chain_t of one or more states representing the initialization values for the sequence of operations in the chain, from first to last
     * @tparam Args The arguments that the result receiver takes. These represent the output(s) from the continuation
     */
    template<typename StateChain, typename... Args>
    struct continuation_t;

    /** Helper trait for identifying if some type is a continuation */
    template<typename T>
    struct is_some_continuation_t : std::bool_constant<false> {};

    template<typename I, typename... A>
    struct is_some_continuation_t<continuation_t<I, A...>> : std::bool_constant<true> {};

    template<typename T>
    static constexpr bool is_some_continuation_v = is_some_continuation_t<T>::value;

    // default inplementation
    template<typename T, typename... Tail, typename... Args>
    struct continuation_t<detail::state_chain_t<T, Tail...>, Args...> {
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");
        static_assert(!std::disjunction_v<is_some_continuation_t<Args>...>, "Argument types must not be continuations");

        using state_chain_type = detail::state_chain_t<T, Tail...>;

        state_chain_type state_chain;

        constexpr continuation_t() = default;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr continuation_t(state_chain_type && state_chain) : state_chain(std::move(state_chain)) {}

        /** Initiate the operation, but do not care about any results from it.  */
        void operator()(SLOC_DEFAULT_ARGUMENT) noexcept
        {
            (*this)(nop_result_receiver_t<Args...>(), nop_exception_receiver_t(), sloc);
        }

        /**
         * Initiate the operation, the result or any exception thrown is passed to the receiver object
         *
         * @tparam Receiver some callable type that takes Args... values as its arguments. This type may be movable or copyable.
         * @tparam Exceptionally some callable type that takes std::exception_ptr as its argument. This type *must* be copyable.
         * @param receiver The receiver callable, that consumes the results from the initiator
         * @param exceptionally The exception handler, that is called if some exception happens.
         */
        template<typename Receiver, typename Exceptionally>
        void operator()(Receiver && receiver, Exceptionally const & exceptionally, SLOC_DEFAULT_ARGUMENT) noexcept
        {
            TRACE_STATE_CHAIN("initiate continuation", sloc, state_chain);

            try {
                using receiver_wrapper = detail::receiver_wrapper_t<Receiver, Args...>;
                using initiation_type = detail::initiation_chain_t<state_chain_type, receiver_wrapper>;
                initiation_type initiator {std::move(state_chain),
                                           receiver_wrapper {sloc, std::forward<Receiver>(receiver)}};
                initiator(exceptionally);
            }
            catch (...) {
                DEBUG_CONTINUATION(sloc, "continuation caught exception");
                exceptionally(std::current_exception());
            }
        }
    };

    template<typename... Args>
    struct continuation_t<detail::polymorphic_state_t<Args...>, Args...> {
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");
        static_assert(!std::disjunction_v<is_some_continuation_t<Args>...>, "Argument types must not be continuations");

        using state_chain_type = detail::polymorphic_state_t<Args...>;

        state_chain_type state_chain;

        /** Default construction is allowed as a special case for the empty Args list, implying that the step is a nop and any input is discarded, and a 'void' value is output to the next step */
        constexpr continuation_t() = default;

        template<typename T, typename... Tail>
        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr continuation_t(continuation_t<detail::state_chain_t<T, Tail...>, Args...> && that)
            : state_chain(std::move(that.state_chain))

        {
        }

        void operator()(SLOC_DEFAULT_ARGUMENT) noexcept
        {
            (*this)(nop_result_receiver_t<Args...>(), nop_exception_receiver_t(), sloc);
        }

        template<typename Receiver, typename Exceptionally>
        void operator()(Receiver && receiver, Exceptionally const & exceptionally, SLOC_DEFAULT_ARGUMENT) noexcept
        {
            TRACE_STATE_CHAIN("initiate continuation", sloc, state_chain);

            try {
                using receiver_wrapper = detail::receiver_wrapper_t<Receiver, Args...>;
                using initiation_type = detail::initiation_chain_t<state_chain_type, receiver_wrapper>;
                initiation_type initiator {std::move(state_chain),
                                           receiver_wrapper {sloc, std::forward<Receiver>(receiver)}};
                initiator(exceptionally);
            }
            catch (...) {
                DEBUG_CONTINUATION(sloc, "continuation caught exception");
                exceptionally(std::current_exception());
            }
        }
    };

    /** Helpful alias for a continuation that has a polymorphic initiator */
    template<typename... Args>
    using polymorphic_continuation_t = continuation_t<detail::polymorphic_state_t<Args...>, Args...>;
}
