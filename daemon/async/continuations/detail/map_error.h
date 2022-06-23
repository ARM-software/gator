/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/map_error_state.h"
#include "lib/source_location.h"

namespace async::continuations::detail {

    /**
     * Factory for constructing a continuation_t for the 'map_error' operation, that validatiates and adapts the error argument so that the exceptional path may be taken
     *
     * @tparam ErrorType The error type
     * @tparam Args The set of additional (non-error) continuation arguments
     */
    template<typename ErrorType, typename... Args>
    class map_error_factory_error_adaptor_t {
    public:
        static_assert(!std::is_reference_v<ErrorType>, "ErrorType types must be value types");
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        /** Create the 'map_error' continuation */
        template<typename FromState>
        static constexpr auto make_continuation(continuation_t<FromState, ErrorType, Args...> && from,
                                                lib::source_loc_t const & sloc)
        {
            using next_factory = continuation_factory_t<Args...>;
            using state_type = map_error_state_t<ErrorType, Args...>;

            return next_factory::make_continuation(std::move(from), state_type {sloc});
        }
    };

    /**
     * Maps certain error types in the first argument to the exception handler path, removing them from the receivers argument set
     *
     * @tparam Args The set of received arguments
     */
    template<typename... Args>
    class map_error_factory_t {
    public:
        static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

        /** Create the 'on_executor' continuation */
        template<typename FromState>
        static constexpr auto make_continuation(continuation_t<FromState, Args...> && from,
                                                lib::source_loc_t const & /*sloc*/)
        {
            return std::move(from);
        }
    };

    // specialization for when the error type is a boost error code
    template<typename... Args>
    class map_error_factory_t<boost::system::error_code, Args...>
        : public map_error_factory_error_adaptor_t<boost::system::error_code, Args...> {
    };

    // specialization for when the error type is a STL error code
    template<typename... Args>
    class map_error_factory_t<std::error_code, Args...>
        : public map_error_factory_error_adaptor_t<std::error_code, Args...> {
    };

    // specialization for when the error type is an exception pointer
    template<typename... Args>
    class map_error_factory_t<std::exception_ptr, Args...>
        : public map_error_factory_error_adaptor_t<std::exception_ptr, Args...> {
    };
}
