/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <exception>
#include <system_error>
#include <type_traits>
#include <utility>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace async::continuations::detail {

    template<typename NextInitiator, typename ErrorType, typename... InputArgs>
    struct map_error_initiator_helper_t;

    /** Receiver type for the case the error argument is a boost error code */
    template<typename NextInitiator, typename... InputArgs>
    struct map_error_initiator_helper_t<NextInitiator, boost::system::error_code, InputArgs...> {
        using error_type = boost::system::error_code;
        using next_type = std::decay_t<NextInitiator>;

        static bool is_error(error_type const & ec) { return !!ec; }

        static std::exception_ptr to_exception_ptr(error_type const & ec)
        {
            return std::make_exception_ptr(boost::system::system_error(ec));
        }

        lib::source_loc_t sloc;
        next_type next;

        template<typename... Args>
        explicit constexpr map_error_initiator_helper_t(lib::source_loc_t const & sloc, Args &&... args)
            : sloc(sloc), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, error_type const & error, InputArgs &&... args)
        {
            if (!is_error(error)) {
                try {
                    next(exceptionally, std::move(args)...);
                }
                catch (...) {
                    TRACE_CONTINUATION(sloc, "map_error: next threw, invoking exceptionally");
                    exceptionally(std::current_exception());
                }
            }
            else if constexpr (std::is_invocable_v<Exceptionally, error_type>) {
                DEBUG_CONTINUATION(sloc,
                                   "map_error: Unexpected error_code={%s:%d} - '%s' received, invoking exceptionally",
                                   error.category().name(),
                                   error.value(),
                                   error.message().c_str());

                exceptionally(error);
            }
            else {
                DEBUG_CONTINUATION(sloc,
                                   "map_error: Unexpected error_code={%s:%d} - '%s' received, invoking exceptionally",
                                   error.category().name(),
                                   error.value(),
                                   error.message().c_str());
                exceptionally(to_exception_ptr(error));
            }
        }
    };

    /** Receiver type for the case the error argument is a STL error code */
    template<typename NextInitiator, typename... InputArgs>
    struct map_error_initiator_helper_t<NextInitiator, std::error_code, InputArgs...> {
        using error_type = std::error_code;
        using next_type = std::decay_t<NextInitiator>;

        static bool is_error(error_type const & ec) { return !!ec; }

        static std::exception_ptr to_exception_ptr(error_type const & ec)
        {
            return std::make_exception_ptr(std::system_error(ec));
        }

        lib::source_loc_t sloc;
        next_type next;

        template<typename... Args>
        explicit constexpr map_error_initiator_helper_t(lib::source_loc_t const & sloc, Args &&... args)
            : sloc(sloc), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, error_type const & error, InputArgs &&... args)
        {
            if (!is_error(error)) {
                try {
                    next(exceptionally, std::move(args)...);
                }
                catch (...) {
                    TRACE_CONTINUATION(sloc, "map_error: next threw, invoking exceptionally");
                    exceptionally(std::current_exception());
                }
            }
            else if constexpr (std::is_invocable_v<Exceptionally, error_type>) {
                DEBUG_CONTINUATION(sloc,
                                   "map_error: Unexpected error_code={%s:%d} - '%s' received, invoking exceptionally",
                                   error.category().name(),
                                   error.value(),
                                   error.message().c_str());

                exceptionally(error);
            }
            else {
                DEBUG_CONTINUATION(sloc,
                                   "map_error: Unexpected error_code={%s:%d} - '%s' received, invoking exceptionally",
                                   error.category().name(),
                                   error.value(),
                                   error.message().c_str());
                exceptionally(to_exception_ptr(error));
            }
        }
    };

    /** Receiver type for the case the error argument is an exception pointer */
    template<typename NextInitiator, typename... InputArgs>
    struct map_error_initiator_helper_t<NextInitiator, std::exception_ptr, InputArgs...> {
        using error_type = std::exception_ptr;
        using next_type = std::decay_t<NextInitiator>;

        static bool is_error(error_type const & ec) { return ec != nullptr; }

        lib::source_loc_t sloc;
        next_type next;

        template<typename... Args>
        explicit constexpr map_error_initiator_helper_t(lib::source_loc_t const & sloc, Args &&... args)
            : sloc(sloc), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, error_type const & error, InputArgs &&... args)
        {
            if (!is_error(error)) {
                try {
                    next(exceptionally, std::move(args)...);
                }
                catch (...) {
                    TRACE_CONTINUATION(sloc, "map_error: next threw, invoking exceptionally");
                    exceptionally(std::current_exception());
                }
            }
            else {
                TRACE_CONTINUATION(sloc, "map_error: Unexpected exception pointer, invoking exceptionally");
                exceptionally(error);
            }
        }
    };

    /** The continuation chain state object for map_error */
    template<typename ErrorType, typename... InputArgs>
    struct map_error_state_t {
        /** Initiator object for map_error */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = map_error_state_t<ErrorType, InputArgs...>;
            using next_type = map_error_initiator_helper_t<NextInitiator, ErrorType, InputArgs...>;

            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : next(state.sloc, std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, ErrorType const & error, InputArgs &&... args)
            {
                next(exceptionally, error, std::move(args)...);
            }
        };

        lib::source_loc_t sloc;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"map_error", sloc}; }
    };
}
