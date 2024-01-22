/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation_traits.h"
#include "async/continuations/detail/polymorphic_state.h"

#include <exception>
#include <memory>
#include <type_traits>

#include <boost/asio/post.hpp>

namespace async::continuations {

    /**
     * A raw (typed) stored continuation, that may be resumed some time later, as per async_initiate_explicit.
     *
     * Intended for use where the polymorphic variant (stored_continuation_t) is not appropriate.
     *
     * @tparam Receiver The raw receiver type
     * @tparam Exceptionally The exceptionally type
     * @tparam Args The continuation arguments
     */
    template<typename Receiver, typename Exceptionally, typename... Args>
    class raw_stored_continuation_t {
    public:
        static_assert(std::is_same_v<Receiver, std::decay_t<Receiver>>);
        static_assert(std::is_same_v<Exceptionally, std::decay_t<Exceptionally>>);

        using receiver_type = Receiver;
        using exceptionally_type = Exceptionally;

        constexpr raw_stored_continuation_t() = default;

        constexpr raw_stored_continuation_t(receiver_type receiver, exceptionally_type exceptionally)
            : receiver(std::move(receiver)), exceptionally(std::move(exceptionally))
        {
        }

        // move only as is required for the receiver
        raw_stored_continuation_t(raw_stored_continuation_t const &) = delete;
        raw_stored_continuation_t & operator=(raw_stored_continuation_t const &) = delete;
        raw_stored_continuation_t(raw_stored_continuation_t &&) noexcept = default;
        raw_stored_continuation_t & operator=(raw_stored_continuation_t &&) noexcept = default;

        // move the receiver (but copy exceptionally so that it remains valid for any subsequent call to get_exceptionally)
        [[nodiscard]] raw_stored_continuation_t<Receiver, Exceptionally, Args...> move()
        {
            return {std::move(receiver), exceptionally};
        }

        [[nodiscard]] exceptionally_type const & get_exceptionally() const { return exceptionally; }

        /**
         * Resume a stored continuation by posting it
         *
         * @param ex_or_ctx The context or executor to run on
         * @param sc The stored continuation to resume
         * -param args The continuation arguments
         */
        template<typename ExOrCtx>
        friend void resume_continuation(ExOrCtx && ex_or_ctx, raw_stored_continuation_t && sc, Args... args)
        {
            boost::asio::post(std::forward<ExOrCtx>(ex_or_ctx),
                              [r = std::move(sc.receiver),
                               e = std::move(sc.exceptionally),
                               args = std::make_tuple(std::move(args)...)]() mutable {
                                  try {
                                      std::apply([&r, &e](Args &&... a) { r(e, std::move(a)...); }, std::move(args));
                                  }
                                  catch (...) {
                                      e(std::current_exception());
                                  }
                              });
        }

        /**
         * Chain a continuation with a stored one such that the continuation's output is passed to the stored one
         *
         * @param ex_or_ctx The context or executor to run on
         * @param continuation The continuation the prepend to the stored continuation
         * @param sc The stored continuation to resume
         */
        template<typename ExOrCtx, typename StateChain>
        friend void submit(ExOrCtx && ex_or_ctx,
                           continuation_t<StateChain, Args...> && continuation,
                           raw_stored_continuation_t && sc)
        {
            boost::asio::post(
                std::forward<ExOrCtx>(ex_or_ctx),
                [r = std::move(sc.receiver), e = std::move(sc.exceptionally), c = std::move(continuation)]() mutable {
                    c([r = std::move(r), e](Args... args) mutable { r(e, std::move(args)...); }, e);
                });
        }

        /**
         * Chain a continuation with a stored one such that the continuation's output is passed to the stored one
         *
         * @param continuation The continuation the prepend to the stored continuation
         * @param sc The stored continuation to resume
         */
        template<typename StateChain>
        friend void submit(continuation_t<StateChain, Args...> && continuation, raw_stored_continuation_t && sc)
        {
            continuation(
                [r = std::move(sc.receiver), e = sc.exceptionally](Args... args) mutable { r(e, std::move(args)...); },
                sc.exceptionally);
        }

        /** to support std::swap */
        void swap(raw_stored_continuation_t & that) noexcept
        {
            receiver_type tmp_r {std::move(receiver)};
            exceptionally_type tmp_e {std::move(exceptionally)};

            receiver = std::move(that.receiver);
            exceptionally = std::move(that.exceptionally);

            that.receiver = std::move(tmp_r);
            that.exceptionally = std::move(tmp_e);
        }

    protected:
        template<typename...>
        friend class stored_continuation_t;

        receiver_type receiver;
        exceptionally_type exceptionally;
    };

    /**
     * A stored continuation, that may be resumed some time later, as per async_initiate_explicit
     *
     * @tparam Args The continuation arguments
     */
    template<typename... Args>
    class stored_continuation_t : public raw_stored_continuation_t<detail::polymorphic_next_initiator_t<Args...>,
                                                                   detail::polymorphic_exceptionally_t,
                                                                   Args...> {
    public:
        using receiver_type = detail::polymorphic_next_initiator_t<Args...>;
        using exceptionally_type = detail::polymorphic_exceptionally_t;
        using parent_type = raw_stored_continuation_t<receiver_type, exceptionally_type, Args...>;

        constexpr stored_continuation_t() = default;

        // NOLINTNEXTLINE(hicpp-explicit-conversions) - allowed
        constexpr stored_continuation_t(raw_stored_continuation_t<receiver_type, exceptionally_type, Args...> && raw)
            : parent_type(std::move(raw.receiver), std::move(raw.exceptionally))
        {
        }

        constexpr stored_continuation_t(receiver_type receiver, exceptionally_type const & exceptionally)
            : parent_type(std::move(receiver), exceptionally)
        {
        }

        template<typename Receiver, typename Exceptionally>
        // NOLINTNEXTLINE(hicpp-explicit-conversions) - allowed
        constexpr stored_continuation_t(raw_stored_continuation_t<Receiver, Exceptionally, Args...> && raw)
            : parent_type(receiver_type::wrap_next_initiator(std::move(raw.receiver)),
                          detail::polymorphic_exceptionally_t::wrap_exceptionally(std::move(raw.exceptionally)))
        {
        }

        template<typename Receiver, typename Exceptionally>
        constexpr stored_continuation_t(Receiver && receiver, Exceptionally const & exceptionally)
            : parent_type(receiver_type::wrap_next_initiator(std::forward<Receiver>(receiver)),
                          exceptionally_type::wrap_exceptionally(exceptionally))
        {
        }

        [[nodiscard]] explicit operator bool() const { return !!parent_type::receiver; }
    };
}

// NOLINTNEXTLINE(cert-dcl58-cpp)
namespace std {
    template<typename R, typename E, typename... Args>
    inline void swap(async::continuations::raw_stored_continuation_t<R, E, Args...> & a,
                     async::continuations::raw_stored_continuation_t<R, E, Args...> & b) noexcept
    {
        a.swap(b);
    }
}
