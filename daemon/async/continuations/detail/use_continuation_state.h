/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {

    /**
     * Used as a Handler for the results of the asio initiator, adapts the output from the initiator to pass the value to our NextInitiator or Exceptionally function
     *
     * @tparam NextInitiator The receiver
     * @tparam Exceptionally The exception handler
     * @tparam Args The handler arguments
     */
    template<typename NextInitiator, typename Exceptionally, typename... Args>
    struct asio_initiator_receiver_handler_t {
    public:
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        using next_type = std::decay_t<NextInitiator>;
        using exceptionally_type = std::decay_t<Exceptionally>;

        constexpr asio_initiator_receiver_handler_t(NextInitiator && receiver, Exceptionally const & exceptionally)
            : receiver(std::forward<NextInitiator>(receiver)), exceptionally(exceptionally)
        {
        }

        /** The default case, where no error mapping occurs */
        void operator()(Args... values)
        {
            try {
                receiver(exceptionally, std::move(values)...);
            }
            catch (...) {
                LOG_DEBUG("use_continuation caught exception");
                exceptionally(std::current_exception());
            }
        }

    private:
        next_type receiver;
        exceptionally_type exceptionally;
    };

    /** Takes the expanded init arguments and passes them to the initiator function passed to async_initiate, to start the operation */
    template<typename Initiator,
             typename NextInitiator,
             template<typename, typename>
             typename Handler,
             typename... InitArgs>
    struct use_continuation_inititator_expanded_t {
        using initiator_type = std::decay_t<Initiator>;
        using next_type = std::decay_t<NextInitiator>;

        initiator_type initiator;
        next_type next;

        template<typename... Args>
        explicit constexpr use_continuation_inititator_expanded_t(Initiator && initiator, Args &&... args)
            : initiator(std::forward<Initiator>(initiator)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, InitArgs &&... args)
        {
            initiator(Handler<next_type, Exceptionally>(std::move(next), exceptionally), std::move(args)...);
        }
    };

    /** The initiator type for use_continuation */
    template<typename Initiator,
             typename Tuple,
             typename NextInitiator,
             template<typename, typename>
             typename Handler,
             typename... OutputArgs>
    struct use_continuation_inititator_t;

    // specialized to extract the InitArgs... type
    template<typename Initiator,
             typename... InitArgs,
             typename NextInitiator,
             template<typename, typename>
             typename Handler,
             typename... OutputArgs>
    struct use_continuation_inititator_t<Initiator, std::tuple<InitArgs...>, NextInitiator, Handler, OutputArgs...> {
        using tuple_type = std::tuple<InitArgs...>;
        using next_type = use_continuation_inititator_expanded_t<Initiator, NextInitiator, Handler, InitArgs...>;

        tuple_type init_args;
        next_type next;

        template<typename... Args>
        explicit constexpr use_continuation_inititator_t(tuple_type && init_args,
                                                         Initiator && initiator,
                                                         Args &&... args)
            : init_args(std::move(init_args)), next(std::forward<Initiator>(initiator), std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally)
        {
            try {
                std::apply([this, &exceptionally](
                               auto &&... args) { next(exceptionally, std::forward<decltype(args)>(args)...); },
                           std::move(init_args));
            }
            catch (...) {
                LOG_DEBUG("use_continuation caught exception from initiation");
                exceptionally(std::current_exception());
            }
        }
    };

    /** The continuation chain state object for use_continuation */
    template<typename Initiator, typename Tuple, typename... OutputArgs>
    struct use_continuation_state_t {
        /** Initiator object for use_continuation */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = use_continuation_state_t<Initiator, Tuple, OutputArgs...>;
            template<typename N, typename E>
            using handler_type = asio_initiator_receiver_handler_t<N, E, OutputArgs...>;
            using next_type = use_continuation_inititator_t<Initiator,
                                                            Tuple,
                                                            std::decay_t<NextInitiator>,
                                                            handler_type,
                                                            OutputArgs...>;

            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : next(std::move(state.init_args), std::move(state.initiator), std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally)
            {
                next(exceptionally);
            }
        };

        Initiator initiator;
        Tuple init_args;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"use_continuation", {}}; }
    };
}
