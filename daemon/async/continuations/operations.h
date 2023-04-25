/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/asio_traits.h"
#include "async/continuations/continuation.h"
#include "async/continuations/continuation_of.h"
#include "async/continuations/detail/cont_if.h"
#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/do_if.h"
#include "async/continuations/detail/loop.h"
#include "async/continuations/detail/map_error.h"
#include "async/continuations/detail/on_executor.h"
#include "async/continuations/detail/predicate.h"
#include "async/continuations/detail/start_state.h"
#include "async/continuations/detail/then.h"
#include "async/continuations/detail/unpack_tuple.h"
#include "async/continuations/detail/unpack_variant.h"
#include "lib/Assert.h"
#include "lib/exception.h"
#include "lib/source_location.h"

#include <cstddef>
#include <exception>
#include <iterator>
#include <type_traits>
#include <utility>
#include <variant>

#include <boost/system/error_code.hpp>

/** A helper that provides the 'detach' like operation, but with debug logging for terminal exceptions */
#define DETACH_LOG_ERROR(name) async::continuations::finally(async::continuations::error_swallower_t {(name)});

namespace async::continuations {
    namespace detail {
        template<typename Op>
        struct then_co_op_t {
            lib::source_loc_t sloc;
            Op op;
        };

        template<typename Op, typename... Args>
        struct unpack_variant_co_op_t {
            lib::source_loc_t sloc;
            Op op;
        };

        template<typename Op>
        struct unpack_variant_detected_co_op_t {
            lib::source_loc_t sloc;
            Op op;
        };

        template<typename Predicate, typename ThenOp, typename ElseOp>
        struct do_if_co_op_t {
            lib::source_loc_t sloc;
            Predicate predicate;
            ThenOp then_op;
            ElseOp else_op;
        };

        template<typename Predicate, typename Generator>
        struct loop_co_op_t {
            lib::source_loc_t sloc;
            Predicate predicate;
            Generator generator;
        };

        struct unpack_variant_detect_type_tag_t;

        template<typename Arg, typename Op>
        struct unpack_variant_co_op_from_t {
            using type = unpack_variant_co_op_t<Op, Arg>;
        };

        template<typename Op>
        struct unpack_variant_co_op_from_t<void, Op> {
            using type = unpack_variant_co_op_t<Op>;
        };

        template<typename Op, typename... Args>
        struct unpack_variant_co_op_from_t<continuation_of_t<Args...>, Op> {
            using type = unpack_variant_co_op_t<Op, Args...>;
        };

        template<typename Op>
        struct unpack_variant_co_op_from_t<unpack_variant_detect_type_tag_t, Op> {
            using type = unpack_variant_detected_co_op_t<Op>;
        };

        struct unpack_tuple_co_op_t {
            lib::source_loc_t sloc;
        };

        template<bool Expected, typename Op>
        struct predicate_co_op_t {
            lib::source_loc_t sloc;
            Op op;
        };

        template<bool Expected>
        struct cont_if_co_op_t {
            lib::source_loc_t sloc;
        };

        template<on_executor_mode_t Mode, typename Executor>
        struct on_executor_co_op_t {
            lib::source_loc_t sloc;
            Executor ex;
        };

        template<bool Discard>
        struct on_map_error_t {
            lib::source_loc_t sloc;
        };

        struct detach_t {
            lib::source_loc_t sloc;
        };

        template<typename Op>
        struct finally_co_op_t {
            lib::source_loc_t sloc;
            Op op;
        };

        template<typename T>
        bool compare_itr(T begin, T end)
        {
            if constexpr (std::is_integral_v<T> || std::is_pointer_v<T>) {
                return begin < end;
            }
            else {
                using iterator_category = typename std::iterator_traits<T>::iterator_category;

                if constexpr (std::is_base_of_v<std::random_access_iterator_tag, iterator_category>) {
                    return begin < end;
                }

                return begin != end;
            }
        }
    }

    /**
     * Start a new continuation chain, with some initial seed values
     * @return A continuation that will, on initiation, directly invoke the receiver via receive_result, passing the specified values
     */
    template<typename... Args>
    constexpr auto start_with(Args &&... args)
    {
        using factory_type = detail::continuation_factory_t<std::decay_t<Args>...>;
        using state_type = detail::start_with_state_t<std::decay_t<Args>...>;

        return factory_type::make_continuation(state_type {std::make_tuple(std::forward<Args>(args)...)});
    }

    /**
     * Start a new continuation chain, with some callable function that produces the initial value
     *
     * @param op A callable object of the form `R()` where R must not be void, nor a continuation_t
     * @return The new continuation
     */
    template<typename Op>
    constexpr auto start_by(Op && op, SLOC_DEFAULT_ARGUMENT)
    {
        using op_type = std::decay_t<Op>;

        static_assert(std::is_invocable_v<op_type>, "then operation is not callable");

        using result_type = std::decay_t<std::invoke_result_t<op_type>>;

        static_assert(!std::is_same_v<void, result_type>, "start_by operation cannot return void");
        static_assert(!is_some_continuation_v<result_type>, "start_by operation cannot return a continuation");

        using factory_type = detail::continuation_factory_t<result_type>;
        using state_type = detail::start_by_state_t<op_type>;

        return factory_type::make_continuation(state_type {sloc, std::forward<Op>(op)});
    }

    /**
     * Constructs a then operation that can be chained to some continuation using | to consume its output
     * @param op Some callable operation that receives the values produced by the preceeding continuation and will produce some next continuation
     * @return The op wrapper object
     */
    template<typename Op>
    constexpr auto then(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        using op_type = std::decay_t<Op>;
        return detail::then_co_op_t<op_type> {sloc, std::move(op)};
    }

    /** Constructs a then operation from a pointer to member function */
    template<typename T, typename R, typename... Args>
    constexpr auto then(T & host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        return then([&host, ptr](Args &&... args) { return (host.*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /** Constructs a then operation from a pointer to member function */
    template<typename T, typename R, typename... Args>
    constexpr auto then(T * host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return then([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /** Constructs a then operation from a pointer to member function, from a shared_ptr */
    template<typename T, typename R, typename... Args>
    constexpr auto then(std::shared_ptr<T> host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return then([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /**
     * Constructs a finally operation that can be chained to some continuation using | to invoke the continuation.
     * @param op The operation takes an exception ptr argument which is null if the continuation terminated without error
     * @return The op wrapper object
     */
    template<typename Op>
    constexpr auto finally(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        using op_type = std::decay_t<Op>;
        return detail::finally_co_op_t<op_type> {sloc, std::move(op)};
    }

    /** Constructs a finally operation from a pointer to member function */
    template<typename T, typename R, typename... Args>
    constexpr auto finally(T & host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        return finally([&host, ptr](Args &&... args) { return (host.*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /** Constructs a finally operation from a pointer to member function */
    template<typename T, typename R, typename... Args>
    constexpr auto finally(T * host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return finally([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /** Constructs a finally operation from a pointer to member function, from a shared_ptr */
    template<typename T, typename R, typename... Args>
    constexpr auto finally(std::shared_ptr<T> host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return finally([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); }, sloc);
    }

    /**
     * Constructs a predicate operation that can be chained to some continuation using | to consume its output
     * @tparam Expected The expected value from the predicate function (true meaning execute if the predicate returns true, false vice versa)
     * @param op Some callable operation that receives the values produced by the preceeding continuation and will produce some next continuation
     * @return The op wrapper object
     */
    template<bool Expected = true, typename Op>
    constexpr auto predicate(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        using op_type = std::decay_t<Op>;
        return detail::predicate_co_op_t<Expected, op_type> {sloc, std::move(op)};
    }

    /** Constructs a predicate operation from a pointer to member function */
    template<bool Expected = true, typename T, typename R, typename... Args>
    constexpr auto predicate(T & host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        return predicate<Expected>([&host, ptr](Args &&... args) { return (host.*ptr)(std::forward<Args>(args)...); },
                                   sloc);
    }

    /** Constructs a predicate operation from a pointer to member function */
    template<bool Expected = true, typename T, typename R, typename... Args>
    constexpr auto predicate(T * host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return predicate<Expected>([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); },
                                   sloc);
    }

    /** Constructs a predicate operation from a pointer to member function, from a shared_ptr */
    template<bool Expected = true, typename T, typename R, typename... Args>
    constexpr auto predicate(std::shared_ptr<T> host, R (T::*ptr)(Args...), SLOC_DEFAULT_ARGUMENT)
    {
        runtime_assert(host != nullptr, "nullptr received");
        return predicate<Expected>([host, ptr](Args &&... args) { return ((*host).*ptr)(std::forward<Args>(args)...); },
                                   sloc);
    }

    /** Constructs a 'continue if' operation that can be chained to some continuation using | that consumes its output,
     * where its first value must be a bool. When the bool is true the chain is continued, otherwise no further continuation is called */
    constexpr auto continue_if_true(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::cont_if_co_op_t<true> {sloc};
    }

    /** Constructs a 'continue if' operation that can be chained to some continuation using | that consumes its output,
     * where its first value must be a bool. When the bool is false the chain is continued, otherwise no further continuation is called */
    constexpr auto continue_if_false(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::cont_if_co_op_t<false> {sloc};
    }

    /**
     * Constructs a do_if operation that can be chained to some continuation using | to consume its output
     * @param predicate The predicate operation that decides whether to call the `then` operation or the `else` operatoin
     * @param then_op Some callable operation that receives the values produced by the preceeding continuation
     * @return The op wrapper object
     */
    template<typename Predicate, typename ThenOp, typename ElseOp>
    constexpr auto do_if_else(Predicate predicate, ThenOp then_op, ElseOp else_op, SLOC_DEFAULT_ARGUMENT)
    {
        using predicate_type = std::decay_t<Predicate>;
        using then_type = std::decay_t<ThenOp>;
        using else_type = std::decay_t<ElseOp>;
        return detail::do_if_co_op_t<predicate_type, then_type, else_type> {sloc,
                                                                            std::move(predicate),
                                                                            std::move(then_op),
                                                                            std::move(else_op)};
    }

    /**
     * Constructs a do_if operation that can be chained to some continuation using | to consume its output
     * @param predicate The predicate operation that is checked to see if the other operation should be called
     * @param op Some callable operation that receives the values produced by the preceeding continuation
     * @return The op wrapper object
     */
    template<typename Predicate, typename Op>
    constexpr auto do_if(Predicate predicate, Op op, SLOC_DEFAULT_ARGUMENT)
    {
        // the else branch does nothing
        return do_if_else(
            std::move(predicate),
            std::move(op),
            [](auto const &... /*args*/) {},
            sloc);
    }

    /**
     * Constructs a 'unpacked std::variant using std::visit' operation that can be chained to some continuation using | so that subsequent operations execute on the supplied executor.
     * @tparam Args A single type being the expected common return type for the next link in the chain. For continuations with multiple arguments pass `continuation_of_t<Args...>`
     * @param op Some callable operation that receives the values produced by the preceeding continuation and will produce some next continuation
     * @return The op wrapper object
     */
    template<typename Args = detail::unpack_variant_detect_type_tag_t, typename Op>
    constexpr auto unpack_variant(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        using op_type = std::decay_t<Op>;
        return typename detail::unpack_variant_co_op_from_t<Args, op_type>::type {sloc, std::move(op)};
    }

    /**
     * Constructs a 'unpacked tuple' operation that can be chained to some continuation using | so that subsequent operations execute on the supplied executor.
     * @return The op wrapper object
     */
    constexpr auto unpack_tuple(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::unpack_tuple_co_op_t {sloc};
    }

    /** Bring the config option in */
    using detail::on_executor_mode_t;

    /**
     * Constructs a 'on executor' operation that can be chained to some continuation using | so that subsequent operations execute on the supplied executor.
     * @tparam Mode The execution mode (one of post, defer, dispatch)
     * @param ex The executor
     * @return The op wrapper object
     */
    template<on_executor_mode_t Mode = on_executor_mode_t::dispatch,
             typename Executor,
             std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    constexpr auto on_executor(Executor const & ex, SLOC_DEFAULT_ARGUMENT)
    {
        return detail::on_executor_co_op_t<Mode, Executor> {sloc, ex};
    }

    /**
     * Constructs a 'on executor' operation that can be chained to some continuation using | so that subsequent operations execute on the supplied executor.
     * @tparam Mode The execution mode (one of post, defer, dispatch)
     * @param context The execution context
     * @return The op wrapper object
     */
    template<on_executor_mode_t Mode = on_executor_mode_t::dispatch,
             typename ExecutionContext,
             std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    constexpr auto on_executor(ExecutionContext & context, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<Mode>(context.get_executor(), sloc);
    }

    /** Simplified on_executor for 'defer' mode */
    template<typename Executor, std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    constexpr auto defer_on(Executor const & ex, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::defer>(ex, sloc);
    }

    /** Simplified on_executor for 'defer' mode */
    template<typename ExecutionContext, std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    constexpr auto defer_on(ExecutionContext & context, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::defer>(context, sloc);
    }

    /** Simplified on_executor for 'dispatch' mode */
    template<typename Executor, std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    constexpr auto dispatch_on(Executor const & ex, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::dispatch>(ex, sloc);
    }

    /** Simplified on_executor for 'dispatch' mode */
    template<typename ExecutionContext, std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    constexpr auto dispatch_on(ExecutionContext & context, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::dispatch>(context, sloc);
    }

    /** Simplified on_executor for 'post' mode */
    template<typename Executor, std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    constexpr auto post_on(Executor const & ex, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::post>(ex, sloc);
    }

    /** Simplified on_executor for 'post' mode */
    template<typename ExecutionContext, std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    constexpr auto post_on(ExecutionContext & context, SLOC_DEFAULT_ARGUMENT)
    {
        return on_executor<on_executor_mode_t::post>(context, sloc);
    }

    /** Creates a 'map error' operation that can be chained to some continuation using | so that any error argument (as the first argument) is mapped to the exception handler */
    constexpr auto map_error(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::on_map_error_t<false> {sloc};
    }

    /** Creates a 'map error' operation that can be chained to some continuation using | so that any error argument (as the first argument) is mapped to the exception handler. Any additional arguments are discareded. */
    constexpr auto map_error_and_discard(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::on_map_error_t<true> {sloc};
    }

    /** Creates a 'detach' operation that can be chained to some continuation using | which invokes the operation with a non-consuming received */
    constexpr auto detach(SLOC_DEFAULT_ARGUMENT)
    {
        return detail::detach_t {sloc};
    }

    /** Chain a continuation and a 'then' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, typename Op>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation, detail::then_co_op_t<Op> && op)
    {
        using op_type = std::decay_t<Op>;

        static_assert(std::is_invocable_v<op_type, std::decay_t<Args>...>,
                      "then operation does not correctly consume preceeding operations output");

        return detail::then_factory_t::make_continuation(std::move(continuation), op.sloc, std::move(op.op));
    }

    /** Chain a continuation and a 'finally' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, typename Op>
    constexpr void operator|(continuation_t<Initiator, Args...> && continuation, detail::finally_co_op_t<Op> && op)
    {
        using op_type = std::decay_t<Op>;

        static_assert(std::is_invocable_v<op_type, std::exception_ptr>,
                      "finally operation does not take an exception_ptr argument");

        continuation(
            [op = op.op, sloc = op.sloc](Args &&... /*args*/) mutable {
                TRACE_CONTINUATION(sloc, "finally completing without error");
                op(nullptr);
            },
            op.op,
            op.sloc);
    }

    /** Chain a continuation and a 'predicate' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, bool Expected, typename Op>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation,
                             detail::predicate_co_op_t<Expected, Op> && op)
    {
        using op_type = std::decay_t<Op>;

        static_assert(std::is_invocable_v<op_type, std::decay_t<Args>...>,
                      "then operation does not correctly consume preceeding operations output");

        using result_type = typename as_continuation_args_t<std::invoke_result_t<op_type, std::decay_t<Args>...>>::type;

        static_assert(std::is_same_v<continuation_of_t<bool>, result_type>,
                      "predicate operation should return bool or continuation thereof");

        using factory_type = detail::predicate_factory_t<Expected, op_type>;

        return factory_type::make_continuation(std::move(continuation), op.sloc, std::move(op.op));
    }

    /** Chain a continuation and a 'continue if' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, bool Expected>
    constexpr auto operator|(continuation_t<Initiator, bool, Args...> && continuation,
                             detail::cont_if_co_op_t<Expected> && op)
    {
        using factory_type = detail::cont_if_factory_t<Expected, Args...>;

        return factory_type::make_continuation(std::move(continuation), op.sloc);
    }

    /** Chain a continuation and a 'do if' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, typename Predicate, typename ThenOp, typename ElseOp>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation,
                             detail::do_if_co_op_t<Predicate, ThenOp, ElseOp> && op)
    {
        using factory_type = detail::do_if_factory_t<Predicate, ThenOp, ElseOp, Args...>;

        return factory_type::make_continuation(std::move(continuation),
                                               op.sloc,
                                               std::move(op.predicate),
                                               std::move(op.then_op),
                                               std::move(op.else_op));
    }

    /** Chain a continuation and a variant unpack operation to produce a new continuation where the result of the first continuation are passed to std::visit and the unpacked value is
     * passed to op's operation for continued processing */
    template<typename Initiator, typename... Variants, typename Op, typename... NextArgs>
    constexpr auto operator|(continuation_t<Initiator, std::variant<Variants...>> && continuation,
                             detail::unpack_variant_co_op_t<Op, NextArgs...> && op)
    {
        using factory_type = detail::unpack_variant_factory_t<NextArgs...>;

        return factory_type::make_continuation(std::move(continuation), op.sloc, std::move(op.op));
    }

    /** Chain a continuation and a variant unpack operation to produce a new continuation where the result of the first continuation are passed to std::visit and the unpacked value is
     * passed to op's operation for continued processing. this variation attempts to detect the common return type for all variations */
    template<typename Initiator, typename... Variants, typename Op>
    constexpr auto operator|(continuation_t<Initiator, std::variant<Variants...>> && continuation,
                             detail::unpack_variant_detected_co_op_t<Op> && op)
    {
        using common_return_type = typename detail::variant_op_common_return_type_t<Op, Variants...>::type;
        using factory_type = typename detail::unpack_variant_factory_from_t<common_return_type>::type;

        return factory_type::make_continuation(std::move(continuation), op.sloc, std::move(op.op));
    }

    template<typename FromInitiator, typename FromTuple>
    constexpr auto operator|(continuation_t<FromInitiator, FromTuple> && continuation,
                             detail::unpack_tuple_co_op_t && op)
    {
        return detail::unpack_tuple_factory_t::make_continuation(std::move(continuation), op.sloc);
    }

    /** Chain a continuation that produces no value, with another continuation */
    template<typename FromInitiator, typename NextInitiator, typename... NextArgs>
    constexpr auto operator|(continuation_t<FromInitiator> && prev_continuation,
                             continuation_t<NextInitiator, NextArgs...> && next_continuation)
    {
        return operator|(std::move(prev_continuation),
                         then([c = std::move(next_continuation)]() mutable { return std::move(c); }));
    }

    /** Chain a continuation and a 'on_executor' operation to produce a new continuation where the results of the first continuation are passed to the operation, but run on a different executor */
    template<typename Initiator, typename... Args, on_executor_mode_t Mode, typename Executor>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation,
                             detail::on_executor_co_op_t<Mode, Executor> && executor)
    {
        static_assert(is_asio_executor_v<Executor>);

        using factory_type = detail::on_executor_factory_t<Mode, Executor, Args...>;

        return factory_type::make_continuation(std::move(continuation), executor.sloc, executor.ex);
    }

    /** Chain a continuation and a 'map_error' operation to produce a new continuation where the results of the first continuation's error argument is converted into an exception */
    template<typename Initiator, typename... Args>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation, detail::on_map_error_t<false> && op)
    {
        using factory_type = detail::map_error_factory_t<Args...>;

        return factory_type::make_continuation(std::move(continuation), op.sloc);
    }

    /** Chain a continuation and a 'map_error_and_discard' operation to produce a new continuation where the results of the first continuation's error argument is converted into an exception */
    template<typename Initiator, typename... Args>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation, detail::on_map_error_t<true> && op)
    {
        using factory_type = detail::map_error_factory_t<Args...>;

        return factory_type::make_continuation(std::move(continuation), op.sloc) //
             | then([](auto const &... /*ignored*/) {}, op.sloc);
    }

    /** Chain a continuation and a 'detach' operation, which terminates the chain and initiates the operation, discarding any outcome. */
    template<typename Initiator, typename... Args>
    constexpr void operator|(continuation_t<Initiator, Args...> && continuation, detail::detach_t && op)
    {
        continuation(op.sloc);
    }

    /**
     * Constructs a new continuation as the composition of `start_with() | on_executor(ex)`
     * @param ex The executor
     * @return The composed continuation
     */
    template<on_executor_mode_t Mode = on_executor_mode_t::post,
             typename Executor,
             std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    constexpr auto start_on(Executor const & ex, SLOC_DEFAULT_ARGUMENT)
    {
        return start_with() | on_executor<Mode>(ex, sloc);
    }

    /**
     * Constructs a new continuation as the composition of `start_with() | on_executor(context)`
     * @param context The execution context
     * @return The composed continuation
     */
    template<on_executor_mode_t Mode = on_executor_mode_t::post,
             typename ExecutionContext,
             std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    constexpr auto start_on(ExecutionContext & context, SLOC_DEFAULT_ARGUMENT)
    {
        return start_on<Mode>(context.get_executor(), sloc);
    }

    /**
     * Constructs a new continuation as the composition of `start_by(op) | continue_if_true()`
     *
     * @param op The predicate operation (must return a bool value)
     * @return The composed continuation
     */
    template<typename Op>
    constexpr auto start_if_true(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        return start_by(std::move(op), sloc) | continue_if_true(sloc);
    }

    /**
     * Constructs a new continuation as the composition of `start_by(op) | continue_if_false()`
     *
     * @param op The predicate operation (must return a bool value)
     * @return The composed continuation
     */
    template<typename Op>
    constexpr auto start_if_false(Op op, SLOC_DEFAULT_ARGUMENT)
    {
        return start_by(std::move(op), sloc) | continue_if_false(sloc);
    }

    /**
     * Constructs a 'loop' continuation operation. This is similar to 'repeatedly' but allows for multiple values to be passed
     * through the predicate and generator stages.
     *
     * @note This class uses recursion to perform the loop, so is best suited to asynchronous loops (for example to implement a virtual thread), or
     * for bounded synchronous loops, otherwise it could eat the stack.
     *
     * @param predicate Must return an implicitly convertible to bool type or a continuation that provides the same
     * @param generator The generator function that must return the continuation for the body of the loop
     * @return The continuation
     */
    template<typename Predicate, typename Generator>
    constexpr auto loop(Predicate && predicate, Generator && generator, SLOC_DEFAULT_ARGUMENT)
    {
        using predicate_type = std::decay_t<Predicate>;
        using generator_type = std::decay_t<Generator>;

        return detail::loop_co_op_t<predicate_type, generator_type> {sloc,
                                                                     std::forward<Predicate>(predicate),
                                                                     std::forward<Generator>(generator)};
    }

    /** Chain a continuation and a 'loop' operation to produce a new continuation where the results of the first continuation are passed to the operation */
    template<typename Initiator, typename... Args, typename Predicate, typename Generator>
    constexpr auto operator|(continuation_t<Initiator, Args...> && continuation,
                             detail::loop_co_op_t<Predicate, Generator> op)
    {
        return detail::loop_factory_t::make_continuation(std::forward<continuation_t<Initiator, Args...>>(continuation),
                                                         op.sloc,
                                                         std::move(op.predicate),
                                                         std::move(op.generator));
    }

    /**
     * Constructs a 'repeatedly' continuation operation. This special continuation will loop calling the predicate function (until it returns false).
     * On each iteration, it will invoke the generator to produce a continuation that is the body of the loop. If the continuation fails exceptionally
     * then the loop will terminate, otherwise any result will be discarded and the loop will repeat.
     * When the loop terminates as a result of the predicate returning false, the next step in the chain will be called, with zero arguments.
     *
     * @note This class uses recursion to perform the loop, so is best suited to asynchronous loops (for example to implement a virtual thread), or
     * for bounded synchronous loops, otherwise it could eat the stack.
     *
     * @param predicate The predicate function that must return bool or a continuation that provides bool
     * @param generator The generator function that must return the continuation for the body of the loop
     * @return The continuation
     */
    template<typename Predicate, typename Generator>
    constexpr auto repeatedly(Predicate && predicate, Generator && generator, SLOC_DEFAULT_ARGUMENT)
    {
        using predicate_type = std::decay_t<Predicate>;
        using generator_type = std::decay_t<Generator>;

        static_assert(std::is_invocable_v<predicate_type>, "predicate operation is not callable");
        static_assert(std::is_invocable_v<generator_type>, "generator operation is not callable");

        using predicate_result_type =
            typename as_continuation_args_t<std::decay_t<std::invoke_result_t<predicate_type>>>::type;
        using generator_result_type =
            typename as_continuation_args_t<std::decay_t<std::invoke_result_t<generator_type>>>::type;

        static_assert(std::is_same_v<continuation_of_t<bool>, predicate_result_type>,
                      "repeatedly requires a predicate that returns bool or a continuation thereof");
        static_assert(std::is_same_v<continuation_of_t<>, generator_result_type>,
                      "repeatedly requires a generator that returns void or a continuation thereof");

        return start_with()
             | detail::loop_co_op_t<predicate_type, generator_type> {sloc,
                                                                     std::forward<Predicate>(predicate),
                                                                     std::forward<Generator>(generator)};
    }

    /**
     * Constructs a continuation operation that iterates from beginning to end
     *
     * @param begin The starting value or begin iterator
     * @param end The end value or end iterator
     * @param op Some operation that takes the current value or iterator as input
     * @return The continuation operation
     */
    template<typename I, typename Op>
    constexpr auto iterate(I begin, I end, Op && op, SLOC_DEFAULT_ARGUMENT)
    {

        return start_with(begin, end) | //
               loop(
                   [](auto it, auto end) { //
                       return start_with(detail::compare_itr(it, end), it, end);
                   },
                   [op = std::forward<Op>(op), sloc](auto it, auto end) {
                       return start_with(it) //
                            | then(op, sloc) //
                            | then([=]() mutable { return start_with(++it, end); }, sloc);
                   },
                   sloc)
             | then([](auto /*it*/, auto /*end*/) {}, sloc); // Absorb the iterators
    }

    /**
     * Constructs a continuation operation that iterates from beginning to end and may hold ownership of the provided iterable
     *
     * @param iterable The iterable
     * @param begin The starting value or begin iterator
     * @param end The end value or end iterator
     * @param op Some operation that takes the current value or iterator as input
     * @return The continuation operation
     */
    template<typename Iterable, typename Iter, typename Op>
    constexpr auto iterate(Iterable && iterable, Iter begin, Iter end, Op && op, SLOC_DEFAULT_ARGUMENT)
    {
        return start_with(begin, end) | //
               loop(
                   [](auto it, auto end) { //
                       return start_with(detail::compare_itr(it, end), it, end);
                   },
                   [op = std::forward<Op>(op), iterable = std::forward<Iterable>(iterable), sloc](auto it, auto end) {
                       return start_with(it) //
                            | then(op, sloc) //
                            | then([=]() mutable { return start_with(++it, end); }, sloc);
                   },
                   sloc)
             | then([](auto /*it*/, auto /*end*/) {}, sloc); // Absorb the iterators
    }

    /**
     * Constructs a continuation operation that iterates from beginning to end and may hold ownership of the provided iterable
     *
     * @param iterable The object to iterate over; may be a l-value reference (in which case something else must own it for the duration of the operation), or a value/r-value (in which case the operation will take ownership)
     * @param op Some operation that takes the current value or iterator as input
     * @return The continuation operation
     */
    template<typename Iterable, typename Op>
    constexpr auto iterate(Iterable && iterable, Op && op, SLOC_DEFAULT_ARGUMENT)
    {
        return iterate(std::forward<Iterable>(iterable), begin(iterable), end(iterable), std::forward<Op>(op), sloc);
    }

    /**
     * Log / swallow unexpected errors from `finally`
     */
    struct error_swallower_t {
        template<typename T>
        static bool consume(char const * name, T const & err)
        {
            error_swallower_t es {name};
            return es(err);
        }

        // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
        char const * name;

        bool operator()(std::nullptr_t) const { return false; }

        bool operator()(boost::system::error_code const & ec) const
        {
            if (ec) {
                LOG_DEBUG("%s failed with error code: %s", name, ec.message().c_str());
                return true;
            }
            return false;
        }

        bool operator()(std::error_code const & ec) const
        {
            if (ec) {
                LOG_DEBUG("%s failed with error code: %s", name, ec.message().c_str());
                return true;
            }
            return false;
        }

        bool operator()(std::exception_ptr const & ep) const
        {
            if (ep != nullptr) {
                LOG_DEBUG("%s failed with exception: %s", name, lib::get_exception_ptr_str(ep));
                return true;
            }
            return false;
        }
    };

    /** Execute a continuation, passing some exception handler. Any results are discarded. Unlike finally, the exception handler is only called on failure. */
    template<typename StateChain, typename... Args, typename Exceptionally>
    void submit(continuation_t<StateChain, Args...> && continuation,
                Exceptionally const & exceptionally,
                SLOC_DEFAULT_ARGUMENT)
    {
        continuation([](Args... /*args*/) {}, exceptionally, sloc);
    }

    /** Spawn a continuation as a virtual thread, calling the handler with a boolean flag to indicate (true) that an error occured */
    template<typename StateChain,
             typename... Args,
             typename Handler,
             std::enable_if_t<std::is_invocable_v<Handler, bool>, bool> = false>
    void spawn(char const * name,
               continuation_t<StateChain, Args...> && continuation,
               Handler && handler,
               SLOC_DEFAULT_ARGUMENT)
    {
        continuation(
            [sloc, h = handler](Args &&... /*args*/) mutable {
                TRACE_CONTINUATION(sloc, "spawn completing without error");
                h(false);
            },
            [h = handler, name](auto const & e) { h(error_swallower_t::consume(name, e)); },
            sloc);
    }

    /** Spawn a continuation as a virtual thread, calling the handler with a boolean flag to indicate (true) that an error occured, with the boost::system::error code extracted as the second argument if the error can be converted to it */
    template<typename StateChain,
             typename... Args,
             typename Handler,
             std::enable_if_t<std::is_invocable_v<Handler, bool, boost::system::error_code>, bool> = false>
    void spawn(char const * name,
               continuation_t<StateChain, Args...> && continuation,
               Handler && handler,
               SLOC_DEFAULT_ARGUMENT)
    {
        continuation(
            [sloc, h = handler](Args &&... /*args*/) mutable {
                TRACE_CONTINUATION(sloc, "spawn completing without error");
                h(false, boost::system::error_code {});
            },
            [h = handler, name](auto const & e) {
                if (error_swallower_t::consume(name, e)) {
                    // store the failure
                    if constexpr (std::is_same_v<boost::system::error_code, std::decay_t<decltype(e)>>) {
                        h(true, e);
                    }
                    else {
                        h(true, boost::system::error_code {});
                    }
                }
                else {
                    h(false, boost::system::error_code {});
                }
            },
            sloc);
    }

    /** Spawn a continuation as a virtual thread */
    template<typename StateChain, typename... Args>
    void spawn(char const * name, continuation_t<StateChain, Args...> && continuation, SLOC_DEFAULT_ARGUMENT)
    {
        continuation(
            [sloc](Args &&... /*args*/) mutable { TRACE_CONTINUATION(sloc, "spawn completing without error"); },
            [name](auto e) { error_swallower_t::consume(name, e); },
            sloc);
    }
}
