/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/continuation.h"
#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "lib/exception.h"
#include "lib/source_location.h"

#include <exception>
#include <type_traits>

#include <boost/asio/async_result.hpp>
#include <boost/system/error_code.hpp>

namespace async::continuations {
    namespace detail {
        /** Helper for async_initiate; specialized for different token types */
        template<typename TokenType, typename ContinuationType>
        struct async_initiate_t;

        /** Initiate a async operation; for use_continuation. Just returns the continuation created by the factory */
        template<typename Allocator, typename... NextArgs>
        struct async_initiate_t<use_continuation_t<Allocator>, continuation_of_t<NextArgs...>> {
            template<typename ContinuationFactory, typename... FactoryArgs>
            static auto initiate(use_continuation_t<Allocator> const & /*token*/,
                                 ContinuationFactory && factory,
                                 FactoryArgs &&... args)
            {
                return factory(std::forward<FactoryArgs>(args)...);
            }
        };

        /** Initiate a async operation; for any other token type. Initiates it; with the generated continuation as the body */
        template<typename TokenType, typename... NextArgs>
        struct async_initiate_t<TokenType, continuation_of_t<NextArgs...>> {
            template<typename CompletionToken, typename ContinuationFactory, typename... FactoryArgs>
            static auto initiate(CompletionToken && token, ContinuationFactory && factory, FactoryArgs &&... args)
            {
                return boost::asio::async_initiate<CompletionToken, void(NextArgs...)>(
                    [factory = std::forward<ContinuationFactory>(factory)](auto && handler,
                                                                           FactoryArgs &&... args) mutable {
                        auto continuation = factory(std::forward<FactoryArgs>(args)...);
                        continuation(std::forward<decltype(handler)>(handler), error_swallower_t {"async_initiate"});
                    },
                    token,
                    std::forward<FactoryArgs>(args)...);
            }
        };

        /** Converts a boost::asio::async_initiate handler to a continuations receiver */
        template<typename Handler, typename... Args>
        struct async_initiate_receiver_to_handler_adaptor_t {
            using handler_type = std::decay_t<Handler>;

            handler_type handler;

            template<typename Exceptionally>
            void operator()(Exceptionally && exceptionally, Args... args)
            {
                try {
                    handler(std::move(args)...);
                }
                catch (...) {
                    LOG_DEBUG("async_initiate_explicit caught exception from receiver");
                    exceptionally(std::current_exception());
                }
            }
        };

        /** Exception handler*/
        struct async_initiate_exceptionally_t {
            void operator()(boost::system::error_code e) const { error_swallower_t::consume("async_init_explicit", e); }
            void operator()(std::exception_ptr e) const { error_swallower_t::consume("async_init_explicit", e); }
        };

        /** Helper for async_initiate_explicit; specialized for different token types */
        template<typename TokenType, typename Signature>
        struct async_initiate_explicit_t;

        /** Initiate a async operation; for use_continuation. Wraps up the initiator function as a continuation, allows the result receiver and exception handler to be passed to
              the initiator separately */
        template<typename Allocator, typename... SigArgs>
        struct async_initiate_explicit_t<use_continuation_t<Allocator>, void(SigArgs...)> {
            /** The continuation chain state object */
            template<typename Initiator, typename... InitArgs>
            struct state_t {
                using boost_initiator_type = std::decay_t<Initiator>;
                using init_args_tuple = std::tuple<std::decay_t<InitArgs>...>;

                /** Initiator object */
                template<typename NextInitiator>
                struct initiator_type {
                    using state_type = state_t;
                    using next_type = std::decay_t<NextInitiator>;

                    state_type state;
                    next_type next;

                    template<typename... Args>
                    explicit constexpr initiator_type(state_type && state, Args &&... args)
                        : state(std::move(state)), next(std::forward<Args>(args)...)
                    {
                    }

                    template<typename Exceptionally>
                    void operator()(Exceptionally const & exceptionally)
                    {
                        try {
                            std::apply(
                                [this, &exceptionally](auto &&... args) mutable {
                                    using stored_continuation_type =
                                        raw_stored_continuation_t<next_type,
                                                                  std::decay_t<Exceptionally>,
                                                                  std::decay_t<SigArgs>...>;

                                    state.initiator(stored_continuation_type(std::move(next), exceptionally),
                                                    std::forward<decltype(args)>(args)...);
                                },
                                std::move(state.init_args));
                        }
                        catch (...) {
                            LOG_DEBUG("async_initiate_explicit caught exception from initiator");
                            exceptionally(std::current_exception());
                        }
                    }
                };

                boost_initiator_type initiator;
                lib::source_loc_t sloc;
                init_args_tuple init_args;

                [[nodiscard]] constexpr name_and_loc_t trace() const { return {"async_initiate_explicit", sloc}; }
            };

            template<typename Initiator, typename... InitArgs>
            static auto initiate(use_continuation_t<Allocator> const & /*token*/,
                                 Initiator && initiator,
                                 lib::source_loc_t sloc,
                                 InitArgs &&... args)
            {
                return detail::continuation_factory_t<std::decay_t<SigArgs>...>::make_continuation(
                    state_t<Initiator, InitArgs...> {
                        std::forward<Initiator>(initiator),
                        sloc,
                        std::make_tuple<std::decay_t<InitArgs>...>(std::forward<InitArgs>(args)...)});
            }
        };

        /** Initiate a async operation; for other completion tokens */
        template<typename TokenType, typename... SigArgs>
        struct async_initiate_explicit_t<TokenType, void(SigArgs...)> {
            template<typename CompletionToken, typename Initiator, typename... InitArgs>
            static auto initiate(CompletionToken && token,
                                 Initiator && initiator,
                                 [[maybe_unused]] lib::source_loc_t sloc,
                                 InitArgs &&... args)
            {
                return boost::asio::async_initiate<CompletionToken, void(SigArgs...)>(
                    [initiator = std::forward<Initiator>(initiator)](auto && handler, InitArgs &&... args) mutable {
                        using handler_type = decltype(handler);
                        using handler_wrapper_type =
                            async_initiate_receiver_to_handler_adaptor_t<std::decay_t<handler_type>,
                                                                         std::decay_t<SigArgs>...>;
                        using stored_continuation_type = raw_stored_continuation_t<handler_wrapper_type,
                                                                                   async_initiate_exceptionally_t,
                                                                                   std::decay_t<SigArgs>...>;

                        initiator(stored_continuation_type(handler_wrapper_type {std::forward<handler_type>(handler)},
                                                           async_initiate_exceptionally_t {}),
                                  std::forward<InitArgs>(args)...);
                    },
                    token,
                    std::forward<InitArgs>(args)...);
            }
        };
    }

    /**
     * Similar to boost::asio::async_initiate, but rather than taking an initiator, takes a factory method that
     * returns a continuation as the body of the async operation.
     *
     * When use_continuation is passed as the completion token, the continuation created by the factory is returned
     * as-is, allowing it to be chained to another continuation.
     * When some-other type is passed as the completion token, the continuation is initiated, with the handler that
     * comes from boost::async::initiate being used as the receiver.
     *
     * @tparam ExpectedType May optionally be specified as `void` (meaning ignored) or `continuation_of_t<...>`, as a
     * way to specify the expected continuation type returned by this call
     * @param factory Some callable that returns continuation_t<...>
     * @param token The completion token
     * @param args Any args to pass to factory
     */
    template<typename ExpectedType = void,
             typename ContinuationFactory,
             typename CompletionToken,
             typename... FactoryArgs>
    auto async_initiate(ContinuationFactory && factory, CompletionToken && token, FactoryArgs &&... args)
    {
        using factory_return_type = std::invoke_result_t<ContinuationFactory, FactoryArgs &&...>;

        static_assert(is_some_continuation_v<factory_return_type>,
                      "Factory method for async_initiate must return a continuation");

        using continuation_args_type = typename as_continuation_args_t<factory_return_type>::type;

        static_assert(std::is_same_v<ExpectedType, void> || std::is_same_v<ExpectedType, continuation_args_type>);

        return detail::async_initiate_t<std::decay_t<CompletionToken>, continuation_args_type>::initiate(
            std::forward<CompletionToken>(token),
            std::forward<ContinuationFactory>(factory),
            std::forward<FactoryArgs>(args)...);
    }

    /**
     * Similar to boost::asio::async_initiate, but rather than providing a single 'handler' to the initator, it provides
     * a 'receiver' and 'exceptionally' callable. The 'receiver' provides the same behaviour as 'handler', and the
     * 'exceptionally' can be passed an std::exception_ptr on failure.
     *
     * When use_continuation is passed as the completion token, a continuation_t is returned, wrapping the initiator, such that
     * it is passed the receiver and exceptionally on invokation of the continuation chain.
     * When some other type is passed, the boost::asio::async_initiate handler is passed as the receiver, and some debug-logging
     * consumer is passed as the exceptionally.
     *
     * This method is primarily intended for use cases where the receiver (and possibly exceptionally) are to be stored for later
     * invokation, rather than chaining.
     *
     * If a continuation chain is used as the body of the initiator function, it must be initiated to start the operation, and it
     * is recomended to do so using `submit(continuation, exceptionally)`.
     *
     * @tparam Signature The receiver's signature (`void(Args...)`) as per boost::asio::async_initiate
     * @param initiator A callable that initiates the operation, taking `receiver, exceptionally, args...` as input
     * @param token The completion token
     * @param sloc Source location
     */
    template<typename Signature, typename Initiator, typename CompletionToken>
    auto async_initiate_explicit(Initiator && initiator, CompletionToken && token, SLOC_DEFAULT_ARGUMENT)
    {
        return detail::async_initiate_explicit_t<std::decay_t<CompletionToken>, Signature>::initiate(
            std::forward<CompletionToken>(token),
            std::forward<Initiator>(initiator),
            sloc);
    }

    /**
     * @param initiator A callable that initiates the operation, taking `receiver, exceptionally, args...` as input
     * @param token The completion token
     * @param sloc Source location
     * @param args Any args to pass to the initiator
     */
    template<typename Signature, typename Initiator, typename CompletionToken, typename... InitArgs>
    auto async_initiate_explicit(Initiator && initiator,
                                 CompletionToken && token,
                                 ::lib::source_loc_t sloc,
                                 InitArgs &&... args)
    {
        return detail::async_initiate_explicit_t<std::decay_t<CompletionToken>, Signature>::initiate(
            std::forward<CompletionToken>(token),
            std::forward<Initiator>(initiator),
            sloc,
            std::forward<InitArgs>(args)...);
    }
}
