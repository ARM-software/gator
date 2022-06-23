/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/detail/continuation_factory.h"
#include "async/continuations/detail/use_continuation_state.h"

#include <memory>
#include <type_traits>

#include <boost/asio/async_result.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace async::continuations {
    namespace detail {
        template<typename... Args>
        struct initiator_factory_t {
            static_assert(!std::disjunction_v<std::is_reference<Args>...>, "Argument types must be value types");

            template<typename Initiator, typename... InitArgs>
            static auto make_asio_state(Initiator && initiator, InitArgs &&... args)
            {
                using state_type =
                    use_continuation_state_t<std::decay_t<Initiator>, std::tuple<std::decay_t<InitArgs>...>, Args...>;

                return state_type {std::forward<Initiator>(initiator),
                                   std::make_tuple(std::forward<InitArgs>(args)...)};
            }
        };
    }

// because of std::allocator<void>
#if defined(__clang__) && defined(_LIBCPP_VERSION)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

    /**
     * use_continuation_t is the type for use boost::asio async operations as the handler (like use_awaitable or use_future)
     *
     * @tparam Allocator The allocator type
     */
    template<typename Allocator = std::allocator<void>>
    class use_continuation_t {
    public:
        using allocator_type = Allocator;

        template<typename InnerExecutor>
        struct executor_with_default : InnerExecutor {
            using default_completion_token_type = use_continuation_t;

            /// Construct the adapted executor from the inner executor type.
            template<
                typename InnerExecutor1,
                std::enable_if_t<std::conjunction_v<std::negate<std::is_same<InnerExecutor1, executor_with_default>>,
                                                    std::is_convertible<InnerExecutor1, InnerExecutor>>,
                                 bool> = false>
            executor_with_default(const InnerExecutor1 & ex) noexcept : InnerExecutor(ex)
            {
            }
        };

        template<typename T>
        using as_default_on_t =
            typename T::template rebind_executor<executor_with_default<typename T::executor_type>>::other;

        /// Function helper to adapt an I/O object to use @c use_continuation_t as its
        /// default completion token type.
        template<typename T>
        static typename std::decay_t<T>::template rebind_executor<
            executor_with_default<typename std::decay_t<T>::executor_type>>::other
        as_default_on(T && object)
        {
            return typename std::decay_t<T>::template rebind_executor<
                executor_with_default<typename std::decay_t<T>::executor_type>>::other(std::forward<T>(object));
        }

        constexpr use_continuation_t() = default;
        explicit constexpr use_continuation_t(const Allocator & allocator) : allocator(allocator) {}

        template<typename OtherAllocator>
        [[nodiscard]] use_continuation_t<OtherAllocator> rebind(const OtherAllocator & allocator) const
        {
            return use_continuation_t<OtherAllocator>(allocator);
        }

        [[nodiscard]] allocator_type get_allocator() const { return allocator; }

    private:
        struct std_allocator_void {
            constexpr std_allocator_void() = default;
            // NOLINTNEXTLINE(hicpp-explicit-conversions)
            [[nodiscard]] operator std::allocator<void>() const { return {}; }
        };

        using real_allocator_type = typename std::
            conditional<std::is_same<std::allocator<void>, Allocator>::value, std_allocator_void, Allocator>::type;

        real_allocator_type allocator;
    };

// because of std::allocator<void>
#if defined(__clang__) && defined(_LIBCPP_VERSION)
#pragma clang diagnostic pop
#endif

    /** Use as the 'handler' for an async operation, errors are forwarded as is */
    static constexpr use_continuation_t<> use_continuation {};
}

namespace boost::asio {

    /** Specialization of async_result for use_continuation_t to initiate the continuation */
    template<typename Allocator, typename Result, typename... Args>
    class async_result<async::continuations::use_continuation_t<Allocator>, Result(Args...)> {
    public:
        template<typename Initiation, typename... InitArgs>
        static auto initiate(Initiation && initiation,
                             async::continuations::use_continuation_t<Allocator> const & /*tag*/,
                             InitArgs... args)
        {
            static_assert(std::conjunction_v<std::is_same<Args, std::decay_t<Args>>...>,
                          "Argument types must be value types");

            using initiator_factory_type = async::continuations::detail::initiator_factory_t<std::decay_t<Args>...>;
            using continuation_factory_type =
                async::continuations::detail::continuation_factory_t<std::decay_t<Args>...>;

            return continuation_factory_type::make_continuation(
                initiator_factory_type::make_asio_state(std::forward<Initiation>(initiation), std::move(args)...));
        }
    };
}
