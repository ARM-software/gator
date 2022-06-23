/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/asio/defer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>

namespace async::continuations::detail {
    /** Possible execution modes */
    enum class on_executor_mode_t {
        /** use boost::asio::dispatch */
        dispatch,
        /** use boost::asio::defer */
        defer,
        /** use boost::asio::post */
        post,
    };

    /** @return a string name for the mode value */
    constexpr char const * mode_name(on_executor_mode_t mode)
    {
        switch (mode) {
            case on_executor_mode_t::defer:
                return "defer";
            case on_executor_mode_t::dispatch:
                return "dispatch";
            case on_executor_mode_t::post:
                return "post";
            default:
                return "???";
        }
    }

    /**
     * Helper that executes some handler on some executor using one of the prefered modes
     *
     * @tparam Mode Specifies how the thing is executed (using asio::dispatch, defer, or post)
     */
    template<on_executor_mode_t Mode>
    struct do_execute_on_t;

    // dispatch the handler on the executor
    template<>
    struct do_execute_on_t<on_executor_mode_t::dispatch> {
        template<typename Ex, typename H>
        static constexpr void execute(Ex const & ex, H && h)
        {
            boost::asio::dispatch(ex, std::forward<H>(h));
        }
    };

    // defer the handler on the executor
    template<>
    struct do_execute_on_t<on_executor_mode_t::defer> {
        template<typename Ex, typename H>
        static constexpr void execute(Ex const & ex, H && h)
        {
            boost::asio::defer(ex, std::forward<H>(h));
        }
    };

    // post the handler on the executor
    template<>
    struct do_execute_on_t<on_executor_mode_t::post> {
        template<typename Ex, typename H>
        static constexpr void execute(Ex const & ex, H && h)
        {
            boost::asio::post(ex, std::forward<H>(h));
        }
    };

    /** The operation to execute on the executor */
    template<on_executor_mode_t Mode, typename Receiver, typename Exceptionally, typename... Args>
    struct on_executor_op_t {
        lib::source_loc_t sloc;
        Receiver receiver;
        Exceptionally exceptionally;
        std::tuple<Args...> args_tuple;

        void operator()()
        {
            TRACE_CONTINUATION(sloc, "on_executor<%s> resuming", mode_name(Mode));

            // invoke the receiver with the args
            try {
                std::apply([this](auto &&... args) { receiver(exceptionally, std::forward<decltype(args)>(args)...); },
                           std::move(args_tuple));
            }
            catch (...) {
                DEBUG_CONTINUATION(sloc, "on_executor<%s> caught exception", mode_name(Mode));

                exceptionally(std::current_exception());
            }
        }
    };

    /** The continuation chain state object for map_error */
    template<on_executor_mode_t Mode, typename Executor, typename... InputArgs>
    struct on_executor_state_t {
        /** Initiator object for on_executor */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = on_executor_state_t<Mode, Executor, InputArgs...>;
            using next_type = std::decay_t<NextInitiator>;

            state_type state;
            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : state(std::move(state)), next(std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
            {
                using op_type = on_executor_op_t<Mode, next_type, std::decay_t<Exceptionally>, InputArgs...>;

                TRACE_CONTINUATION(state.sloc, "on_executor<%s> submitting", mode_name(Mode));

                // execute on the new executor
                do_execute_on_t<Mode>::execute(state.executor,
                                               op_type {state.sloc,
                                                        std::move(next),
                                                        exceptionally,
                                                        std::make_tuple<InputArgs...>(std::move(args)...)});
            }
        };

        lib::source_loc_t sloc;
        Executor executor;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"on_executor", sloc}; }
    };

}
