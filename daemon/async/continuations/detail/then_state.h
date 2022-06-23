/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/initiation_chain.h"
#include "async/continuations/detail/trace.h"
#include "lib/source_location.h"

#include <exception>
#include <type_traits>
#include <utility>

namespace async::continuations::detail {
    /** Initiator helper for then that returns some value */
    template<typename ResultType, typename... InputArgs>
    struct then_initiator_helper_t {
        using result_type = std::decay_t<ResultType>;
        using next_factory = continuation_factory_t<result_type>;

        /** Execute the operation, passing it the input arguments, and passing its result to the next stage */
        template<typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate(lib::source_loc_t const & /*sloc*/,
                                       Op && op,
                                       NextInitiator && next,
                                       Exceptionally const & exceptionally,
                                       InputArgs &&... args)
        {
            next(exceptionally, op(std::move(args)...));
        }

        /** Execute the operation, passing it the input arguments, and passing its result along with the next stage and exception handler to the post processor */
        template<typename PostProcess, typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate_with_post_process(lib::source_loc_t const & /*sloc*/,
                                                         PostProcess && post_process,
                                                         Op && op,
                                                         NextInitiator && next,
                                                         Exceptionally const & exceptionally,
                                                         InputArgs &&... args)
        {
            post_process(std::forward<NextInitiator>(next), exceptionally, op(std::move(args)...));
        }
    };

    /** Initiator helper for then that returns void */
    template<typename... InputArgs>
    struct then_initiator_helper_t<void, InputArgs...> {
        using result_type = void;
        using next_factory = continuation_factory_t<>;

        /** Execute the operation, passing it the input arguments, and passing its result to the next stage */
        template<typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate(lib::source_loc_t const & /*sloc*/,
                                       Op && op,
                                       NextInitiator && next,
                                       Exceptionally const & exceptionally,
                                       InputArgs &&... args)
        {
            op(std::move(args)...);
            next(exceptionally);
        }

        /** Execute the operation, passing it the input arguments, and passing its result along with the next stage and exception handler to the post processor */
        template<typename PostProcess, typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate_with_post_process(lib::source_loc_t const & /*sloc*/,
                                                         PostProcess && post_process,
                                                         Op && op,
                                                         NextInitiator && next,
                                                         Exceptionally const & exceptionally,
                                                         InputArgs &&... args)
        {
            op(std::move(args)...);
            post_process(std::forward<NextInitiator>(next), exceptionally);
        }
    };

    /** Receiver wrapper, that post processes the output of a continuation */
    template<typename PostProcess, typename NextInitiator, typename... InputArgs>
    struct then_continuation_post_process_t {
        using post_process_type = std::decay_t<PostProcess>;
        using next_type = std::decay_t<NextInitiator>;

        post_process_type post_process;
        next_type next;

        template<typename... Args>
        // NOLINTNEXTLINE(hicpp-explicit-conversions)
        constexpr then_continuation_post_process_t(PostProcess && post_process, Args &&... args)
            : post_process(std::move(post_process)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            post_process(std::move(next), exceptionally, std::move(args)...);
        }
    };

    /** Initiator helper for then that returns another continuation chain */
    template<typename StateChain, typename... OutputArgs, typename... InputArgs>
    struct then_initiator_helper_t<continuation_t<StateChain, OutputArgs...>, InputArgs...> {
        using result_type = continuation_t<StateChain, OutputArgs...>;
        using next_factory = continuation_factory_t<OutputArgs...>;

        /** Execute the operation, passing it the input arguments, and passing its result to the next stage */
        template<typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate(lib::source_loc_t const & sloc,
                                       Op && op,
                                       NextInitiator && next,
                                       Exceptionally const & exceptionally,
                                       InputArgs &&... args)
        {
            using initiation_type = initiation_chain_t<StateChain, std::decay_t<NextInitiator>>;
            initiation_type initiator {get_state_chain(sloc, op(std::move(args)...)), //
                                       std::forward<NextInitiator>(next)};
            initiator(exceptionally);
        }

        /** Execute the operation, passing it the input arguments, and passing its result along with the next stage and exception handler to the post processor */
        template<typename PostProcess, typename Op, typename NextInitiator, typename Exceptionally>
        static constexpr void initiate_with_post_process(lib::source_loc_t const & sloc,
                                                         PostProcess && post_process,
                                                         Op && op,
                                                         NextInitiator && next,
                                                         Exceptionally const & exceptionally,
                                                         InputArgs &&... args)
        {
            using initiation_type = initiation_chain_t<StateChain,
                                                       then_continuation_post_process_t<std::decay_t<PostProcess>,
                                                                                        std::decay_t<NextInitiator>,
                                                                                        OutputArgs...>>;
            initiation_type initiator {get_state_chain(sloc, op(std::move(args)...)),
                                       std::forward<PostProcess>(post_process),
                                       std::forward<NextInitiator>(next)};
            initiator(exceptionally);
        }

        /** @return the state chain from the continuation, after trace logging */
        static constexpr StateChain && get_state_chain(lib::source_loc_t const & sloc,
                                                       continuation_t<StateChain, OutputArgs...> && continuation)
        {
            TRACE_STATE_CHAIN("then", sloc, continuation.state_chain);

            return std::move(continuation.state_chain);
        }
    };

    /** Initiator wrapper type */
    template<typename Op, typename ResultType, typename NextInitiator, typename... InputArgs>
    struct then_initiator_t {
        using op_type = std::decay_t<Op>;
        using next_type = std::decay_t<NextInitiator>;
        using helper_type = then_initiator_helper_t<std::decay_t<ResultType>, InputArgs...>;

        lib::source_loc_t sloc;
        op_type op;
        next_type next;

        template<typename... Args>
        constexpr then_initiator_t(lib::source_loc_t const & sloc, Op && op, Args &&... args)
            : sloc(sloc), op(std::move(op)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            TRACE_CONTINUATION(sloc, "then");
            helper_type::initiate(sloc, std::move(op), std::move(next), exceptionally, std::move(args)...);
        }
    };

    /** Initiator wrapper type for fixed post-processing */
    template<typename Op, typename PostProcess, typename ResultType, typename NextInitiator, typename... InputArgs>
    struct then_initiator_with_post_process_t {
        using op_type = std::decay_t<Op>;
        using post_process_type = std::decay_t<PostProcess>;
        using next_type = std::decay_t<NextInitiator>;
        using helper_type = then_initiator_helper_t<std::decay_t<ResultType>, InputArgs...>;

        lib::source_loc_t sloc;
        op_type op;
        post_process_type post_process;
        next_type next;

        template<typename... Args>
        explicit constexpr then_initiator_with_post_process_t(lib::source_loc_t const & sloc,
                                                              Op && op,
                                                              PostProcess && post_process,
                                                              Args &&... args)
            : sloc(sloc), op(std::move(op)), post_process(std::move(post_process)), next(std::forward<Args>(args)...)
        {
        }

        template<typename Exceptionally>
        void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
        {
            helper_type::initiate_with_post_process(sloc,
                                                    std::move(post_process),
                                                    std::move(op),
                                                    std::move(next),
                                                    exceptionally,
                                                    std::move(args)...);
        }
    };

    /** Helper for performing then operations */
    template<typename Op, typename... InputArgs>
    struct then_helper_t {
        using op_type = std::decay_t<Op>;

        static_assert(std::is_invocable_v<op_type, InputArgs...>, "Operation cannot be called with provided arguments");

        using op_return_type = std::decay_t<std::invoke_result_t<op_type, InputArgs...>>;
        using initiator_helper_type = then_initiator_helper_t<op_return_type, InputArgs...>;
        using next_factory = typename initiator_helper_type::next_factory;

        template<typename NextInitiator>
        using initiator_type = then_initiator_t<op_type, op_return_type, NextInitiator, InputArgs...>;

        template<typename PostProcess, typename NextInitiator>
        using initiator_with_post_process_type = then_initiator_with_post_process_t<op_type,
                                                                                    std::decay_t<PostProcess>,
                                                                                    op_return_type,
                                                                                    NextInitiator,
                                                                                    InputArgs...>;
    };

    /** The continuation chain state object for then */
    template<typename Op, typename... InputArgs>
    struct then_state_t {
        /** Initiator object for then */
        template<typename NextInitiator>
        struct initiator_type {
            using state_type = then_state_t<Op, InputArgs...>;
            using helper_type = then_helper_t<Op, InputArgs...>;
            using next_type = typename helper_type::template initiator_type<std::decay_t<NextInitiator>>;

            next_type next;

            template<typename... Args>
            explicit constexpr initiator_type(state_type && state, Args &&... args)
                : next(state.sloc, std::move(state.op), std::forward<Args>(args)...)
            {
            }

            template<typename Exceptionally>
            void operator()(Exceptionally const & exceptionally, InputArgs &&... args)
            {
                next(exceptionally, std::move(args)...);
            }
        };

        lib::source_loc_t sloc;
        Op op;

        [[nodiscard]] constexpr name_and_loc_t trace() const { return {"then", sloc}; }
    };
}
