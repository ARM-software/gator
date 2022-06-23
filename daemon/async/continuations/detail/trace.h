/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Config.h"
#include "Logging.h"
#include "lib/source_location.h"

#define DEBUG_CONTINUATION(sloc, format, ...)                                                                          \
    do {                                                                                                               \
        ::logging::detail::do_log_item(::logging::log_level_t::debug, sloc, (format), ##__VA_ARGS__);                  \
    } while (false)

// continuation tracing is expensive and only available on debug builds (and only outputs when --trace is set)
#if (!defined(NDEBUG)                                                                                                  \
     && (!defined(CONFIG_DISABLE_CONTINUATION_TRACING) || (CONFIG_DISABLE_CONTINUATION_TRACING == 0)))                 \
    || (defined(GATOR_UNIT_TESTS) && (GATOR_UNIT_TESTS != 0))

#include "lib/Span.h"

#include <array>
#include <cstddef>
#include <string_view>

#define TRACE_CONTINUATION(sloc, format, ...)                                                                          \
    do {                                                                                                               \
        if (::logging::is_log_enable_trace()) {                                                                        \
            ::logging::detail::do_log_item(::logging::log_level_t::trace,                                              \
                                           sloc,                                                                       \
                                           ("TRACE CONTINUATION:  " format),                                           \
                                           ##__VA_ARGS__);                                                             \
        }                                                                                                              \
    } while (false)

#define TRACE_STATE_CHAIN(reason, sloc, state_chain)                                                                   \
    do {                                                                                                               \
        if (::logging::is_log_enable_trace()) {                                                                        \
            using helper_type =                                                                                        \
                ::async::continuations::detail::state_chain_sloc_details_t<std::decay_t<decltype(state_chain)>>;       \
            std::array<::async::continuations::detail::name_and_loc_t, helper_type::total_depth> nslocs {};            \
            helper_type::template trace<0>(state_chain, nslocs);                                                       \
            ::async::continuations::detail::trace_state_chain((reason), (sloc), nslocs);                               \
        }                                                                                                              \
    } while (false)

namespace async::continuations::detail {
    // forward decls
    template<typename... Args>
    class polymorphic_state_t;
    template<typename T, typename... Tail>
    struct state_chain_t;

    template<typename StateChain>
    struct state_chain_sloc_details_t;

    struct name_and_loc_t {
        std::string_view name;
        lib::source_loc_t sloc;
    };

    template<typename T, typename... Tail>
    struct state_chain_sloc_details_t<state_chain_t<T, Tail...>> {
        using state_chain_type = state_chain_t<T, Tail...>;
        using next_type = state_chain_sloc_details_t<state_chain_t<Tail...>>;

        static constexpr std::size_t total_depth = 1 + next_type::total_depth;

        template<std::size_t O, std::size_t N>
        static constexpr void trace(state_chain_type const & state_chain, std::array<name_and_loc_t, N> & container)
        {
            static_assert(N >= total_depth);
            static_assert(O < N);
            container[O] = state_chain.value.trace();
            next_type::template trace<O + 1>(state_chain.next, container);
        }
    };

    template<typename T>
    struct state_chain_sloc_details_t<state_chain_t<T>> {
        using state_chain_type = state_chain_t<T>;

        static constexpr std::size_t total_depth = 1;

        template<std::size_t O, std::size_t N>
        static constexpr void trace(state_chain_type const & state_chain, std::array<name_and_loc_t, N> & container)
        {
            static_assert(N >= total_depth);
            static_assert(O < N);
            container[O] = state_chain.value.trace();
        }
    };

    template<typename... Args>
    struct state_chain_sloc_details_t<polymorphic_state_t<Args...>> {
        using state_chain_type = polymorphic_state_t<Args...>;

        static constexpr std::size_t total_depth = 1;

        template<std::size_t O, std::size_t N>
        static constexpr void trace(state_chain_type const & state_chain, std::array<name_and_loc_t, N> & container)
        {
            static_assert(N >= total_depth);
            static_assert(O < N);
            container[O] = state_chain.trace();
        }
    };

    inline void trace_state_chain(std::string_view reason,
                                  lib::source_loc_t const & sloc,
                                  lib::Span<name_and_loc_t> chain)
    {
        ::logging::detail::do_log_item(::logging::log_level_t::trace, sloc, "TRACE CONTINUATION %s", reason.data());
        for (auto & entry : chain) {
            ::logging::detail::do_log_item(::logging::log_level_t::trace, entry.sloc, "    -> %s", entry.name.data());
        }
    }
}

#else

#define TRACE_CONTINUATION(sloc, format, ...) (void) sloc
#define TRACE_STATE_CHAIN(reason, sloc, state_chain) (void) sloc

namespace async::continuations::detail {
    struct name_and_loc_t {
        std::string_view name;
        lib::source_loc_t sloc;
    };
}

#endif
