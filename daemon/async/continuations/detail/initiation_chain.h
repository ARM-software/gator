/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    // forward decls
    template<typename... Args>
    class polymorphic_state_t;
    template<typename T, typename... Tail>
    struct state_chain_t;

    /** Wraps the user provided receiver, so that it looks like an initiator */
    template<typename Receiver, typename... Args>
    struct receiver_wrapper_t {
        using receiver_type = std::decay_t<Receiver>;

        lib::source_loc_t sloc;
        receiver_type receiver;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr receiver_wrapper_t(lib::source_loc_t const & sloc, Receiver && receiver)
            : sloc(sloc), receiver(std::forward<Receiver>(receiver))
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & /* exceptionally*/, Args &&... args)
        {
            TRACE_CONTINUATION(sloc, "invoking receiver");
            receiver(std::move(args)...);
        }
    };

    /** Converts the state_chain_t to a sequence of nested initiators representing the sequence of operations to perform.
     * Each nested item in the chain can be moved as and when required (for example when it needs to be saved for later use by
     * an async operation, or the append to a received continuation) */
    template<typename StateChain, typename FinalReceiver>
    struct initiation_chain_t;

    // Construct from an entry in the chain
    template<typename T, typename... Tail, typename FinalReceiver>
    struct initiation_chain_t<state_chain_t<T, Tail...>, FinalReceiver> {
        using state_chain_type = state_chain_t<T, Tail...>;
        using next_type = initiation_chain_t<state_chain_t<Tail...>, FinalReceiver>;
        using initiator_type = typename T::template initiator_type<next_type>;

        initiator_type initiator;

        template<typename... Args>
        explicit constexpr initiation_chain_t(state_chain_type && state_chain, Args &&... args)
            : initiator(std::move(state_chain.value), std::move(state_chain.next), std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally, typename... InputArgs>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            initiator(exceptionally, std::forward<InputArgs>(args)...);
        }
    };

    // Construct from the final entry in the chain
    template<typename T, typename FinalReceiver>
    struct initiation_chain_t<state_chain_t<T>, FinalReceiver> {
        using state_chain_type = state_chain_t<T>;
        using next_type = FinalReceiver;
        using initiator_type = typename T::template initiator_type<next_type>;

        initiator_type initiator;

        template<typename... Args>
        explicit constexpr initiation_chain_t(state_chain_type && state_chain, Args &&... args)
            : initiator(std::move(state_chain.value), std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally, typename... InputArgs>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            initiator(exceptionally, std::forward<InputArgs>(args)...);
        }
    };

    // Construct for a polymorphic chain item
    template<typename... Args, typename FinalReceiver>
    struct initiation_chain_t<polymorphic_state_t<Args...>, FinalReceiver> {
        using state_chain_type = polymorphic_state_t<Args...>;
        using next_type = FinalReceiver;
        using initiator_type = typename polymorphic_state_t<Args...>::template initiator_type<next_type>;

        initiator_type initiator;

        template<typename... A>
        explicit constexpr initiation_chain_t(state_chain_type && state_chain, A &&... args)
            : initiator(std::move(state_chain), std::forward<A>(args)...)
        {
        }

        template<typename Exceptionally, typename... InputArgs>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            initiator(exceptionally, std::forward<InputArgs>(args)...);
        }
    };
}
