/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/initiation_chain.h"
#include "async/continuations/detail/state_chain.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <exception>
#include <memory>
#include <system_error>
#include <type_traits>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace async::continuations::detail {
    // ---------------------

    /** Base type for wrapper around some Exceptionally that type erases it */
    class polymorphic_exceptionally_base_t {
    public:
        virtual ~polymorphic_exceptionally_base_t() noexcept = default;

        virtual void operator()(boost::system::error_code const & ec) const = 0;
        virtual void operator()(std::error_code const & ec) const = 0;
        virtual void operator()(std::exception_ptr const & ep) const = 0;
    };

    /** Concrete type for wrapper around some Exceptionally that type erases it */
    template<typename Exceptionally>
    class polymorphic_exceptionally_value_t : public polymorphic_exceptionally_base_t {
    public:
        using exceptionally_type = std::decay_t<Exceptionally>;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr polymorphic_exceptionally_value_t(Exceptionally const & exceptionally) : exceptionally(exceptionally)
        {
        }

        void operator()(boost::system::error_code const & ec) const override
        {
            if constexpr (std::is_invocable_v<exceptionally_type, boost::system::error_code const &>) {
                exceptionally(ec);
            }
            else {
                exceptionally(std::make_exception_ptr(boost::system::system_error(ec)));
            }
        }

        void operator()(std::error_code const & ec) const override
        {
            if constexpr (std::is_invocable_v<exceptionally_type, std::error_code const &>) {
                exceptionally(ec);
            }
            else {
                exceptionally(std::make_exception_ptr(std::system_error(ec)));
            }
        }

        void operator()(std::exception_ptr const & ep) const override { exceptionally(ep); }

    private:
        exceptionally_type exceptionally;
    };

    /** Wrapper around some Exceptionally that type erases it */
    class polymorphic_exceptionally_t {
    public:
        template<typename Exceptionally>
        static polymorphic_exceptionally_t wrap_exceptionally(Exceptionally const & exceptionally)
        {
            using value_type = polymorphic_exceptionally_value_t<Exceptionally>;

            return {std::make_shared<value_type>(exceptionally)};
        }

        static polymorphic_exceptionally_t wrap_exceptionally(polymorphic_exceptionally_t const & exceptionally)
        {
            return exceptionally;
        }

        constexpr polymorphic_exceptionally_t() = default;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        polymorphic_exceptionally_t(std::shared_ptr<polymorphic_exceptionally_base_t> && exceptionally)
            : exceptionally(exceptionally)
        {
        }

        void operator()(boost::system::error_code const & ec) const { (*exceptionally)(ec); }

        void operator()(std::error_code const & ec) const { (*exceptionally)(ec); }

        void operator()(std::exception_ptr const & ep) const { (*exceptionally)(ep); }

    private:
        std::shared_ptr<polymorphic_exceptionally_base_t> exceptionally;
    };

    // ---------------------

    /** Base type for wrapper around some NextInitiator that type erases it */
    template<typename... InputArgs>
    class polymorphic_next_initiator_base_t {
    public:
        virtual ~polymorphic_next_initiator_base_t() noexcept = default;

        virtual void operator()(polymorphic_exceptionally_t const & exceptionally, InputArgs &&... args) = 0;
    };

    /** Concrete type for wrapper around some NextInitiator that type erases it */
    template<typename NextInitiator, typename... InputArgs>
    class polymorphic_next_initiator_value_t : public polymorphic_next_initiator_base_t<InputArgs...> {
    public:
        using next_type = std::decay_t<NextInitiator>;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr polymorphic_next_initiator_value_t(NextInitiator && next_initiator)
            : next_initiator(std::forward<NextInitiator>(next_initiator))
        {
        }

        void operator()(polymorphic_exceptionally_t const & exceptionally, InputArgs &&... args) override
        {
            next_initiator(exceptionally, std::move(args)...);
        }

    private:
        next_type next_initiator;
    };

    /** Wrapper around some NextInitiator that type erases it */
    template<typename... InputArgs>
    class polymorphic_next_initiator_t {
    public:
        template<typename NextInitiator>
        static polymorphic_next_initiator_t wrap_next_initiator(NextInitiator && next_initiator)
        {
            using value_type = polymorphic_next_initiator_value_t<NextInitiator, InputArgs...>;

            return {std::make_unique<value_type>(std::forward<NextInitiator>(next_initiator))};
        }

        static polymorphic_next_initiator_t wrap_next_initiator(polymorphic_next_initiator_t next_initiator)
        {
            return next_initiator;
        }

        constexpr polymorphic_next_initiator_t() = default;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        polymorphic_next_initiator_t(std::unique_ptr<polymorphic_next_initiator_base_t<InputArgs...>> && next_initiator)
            : next_initiator(std::move(next_initiator))
        {
        }

        [[nodiscard]] explicit operator bool() const { return !!next_initiator; }

        void operator()(polymorphic_exceptionally_t const & exceptionally, InputArgs &&... args)
        {
            std::unique_ptr<polymorphic_next_initiator_base_t<InputArgs...>> next_initiator {
                std::move(this->next_initiator)};

            (*next_initiator)(exceptionally, std::move(args)...);
        }

    private:
        std::unique_ptr<polymorphic_next_initiator_base_t<InputArgs...>> next_initiator;
    };

    // ---------------------

    /** Base type for polymorphic state type */
    template<typename... OutputArgs>
    class polymorphic_state_base_t {
    public:
        virtual ~polymorphic_state_base_t() noexcept = default;

        virtual void operator()(lib::source_loc_t const & sloc,
                                polymorphic_next_initiator_t<OutputArgs...> && next,
                                polymorphic_exceptionally_t const & exceptionally) = 0;
    };

    /** Concrete state wrapper */
    template<typename StateChain, typename... OutputArgs>
    class polymorphic_state_value_t;

    template<typename T, typename... Tail, typename... OutputArgs>
    class polymorphic_state_value_t<state_chain_t<T, Tail...>, OutputArgs...>
        : public polymorphic_state_base_t<OutputArgs...> {
    public:
        using state_chain_type = state_chain_t<T, Tail...>;

        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr polymorphic_state_value_t(state_chain_type && state_chain) : state_chain(std::move(state_chain)) {}

        void operator()(lib::source_loc_t const & sloc,
                        polymorphic_next_initiator_t<OutputArgs...> && next,
                        polymorphic_exceptionally_t const & exceptionally) override
        {
            TRACE_STATE_CHAIN("polymorphic initiate", sloc, state_chain);

            using initiation_type = initiation_chain_t<state_chain_type, polymorphic_next_initiator_t<OutputArgs...>>;
            initiation_type initiator {std::move(state_chain), std::move(next)};
            initiator(exceptionally);
        }

    private:
        state_chain_type state_chain;
    };

    // forward decl for initiator
    template<typename... OutputArgs>
    class polymorphic_state_t;

    /** Initiator object for type erased state chain */
    template<typename NextInitiator, typename... OutputArgs>
    struct polymorphic_state_initiator_t {
        using state_type = polymorphic_state_t<OutputArgs...>;
        using next_type = std::decay_t<NextInitiator>;

        state_type state;
        next_type next;

        template<typename... Args>
        explicit constexpr polymorphic_state_initiator_t(state_type && state, Args &&... args)
            : state(std::move(state)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally)
        {
            state(polymorphic_next_initiator_t<OutputArgs...>::wrap_next_initiator(std::move(next)),
                  polymorphic_exceptionally_t::wrap_exceptionally(exceptionally));
        }
    };

    /** Wrapper around the type erased base type pointer */
    template<typename... OutputArgs>
    class polymorphic_state_t {
    public:
        template<typename NextInitiator>
        using initiator_type = polymorphic_state_initiator_t<NextInitiator, OutputArgs...>;

        template<typename T, typename... Tail>
        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr polymorphic_state_t(state_chain_t<T, Tail...> && state_chain, SLOC_DEFAULT_ARGUMENT)
            : state_chain(std::make_unique<polymorphic_state_value_t<state_chain_t<T, Tail...>, OutputArgs...>>(
                std::move(state_chain))),
              sloc(sloc)
        {
        }

        void operator()(polymorphic_next_initiator_t<OutputArgs...> && next,
                        polymorphic_exceptionally_t const & exceptionally)
        {
            TRACE_CONTINUATION(sloc, "polymorphic_state");

            (*state_chain)(sloc, std::move(next), exceptionally);
        }

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"polymorphic_state", sloc}; }

    private:
        std::unique_ptr<polymorphic_state_base_t<OutputArgs...>> state_chain;
        lib::source_loc_t sloc;
    };

    /** Wrapper around the type erased base type pointer (for no args, allows default construction, which is NOP) */
    template<>
    class polymorphic_state_t<> {
    public:
        template<typename NextInitiator>
        using initiator_type = polymorphic_state_initiator_t<NextInitiator>;

        constexpr polymorphic_state_t() = default;

        template<typename T, typename... Tail>
        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr polymorphic_state_t(state_chain_t<T, Tail...> && state_chain, SLOC_DEFAULT_ARGUMENT)
            : state_chain(
                std::make_unique<polymorphic_state_value_t<state_chain_t<T, Tail...>>>(std::move(state_chain))),
              sloc(sloc)
        {
        }

        void operator()(polymorphic_next_initiator_t<> && next, polymorphic_exceptionally_t const & exceptionally)
        {
            TRACE_CONTINUATION(sloc, "polymorphic_state");

            if (state_chain) {

                (*state_chain)(sloc, std::move(next), exceptionally);
            }
            else {
                next(exceptionally);
            }
        }

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"polymorphic_state", sloc}; }

    private:
        std::unique_ptr<polymorphic_state_base_t<>> state_chain;
        lib::source_loc_t sloc;
    };
}
