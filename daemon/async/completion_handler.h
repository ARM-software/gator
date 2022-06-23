/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "async/asio_traits.h"

#include <functional>
#include <type_traits>

namespace async {
    /**
     * The base class for completion_handler_t which allows us to type-erase the actual handler object
     */
    template<typename... Args>
    class completion_handler_base_t {
    public:
        virtual ~completion_handler_base_t() noexcept = default;
        virtual void operator()(Args &&... args) = 0;
    };

    /** Typed implementation of completion_handler_base_t */
    template<typename Handler, typename... Args>
    class completion_handler_t : public completion_handler_base_t<Args...> {
    public:
        static_assert(!std::is_reference_v<Handler>, "Handler must not be a reference type");

        constexpr explicit completion_handler_t(Handler && handler) : handler(std::forward<Handler>(handler)) {}
        constexpr void operator()(Args &&... args) override
        {
            static_assert(std::is_invocable_v<Handler, Args...>);
            handler(std::forward<Args>(args)...);
        }

    private:
        Handler handler;
    };

    /** A type-erased callable container for some completion handler */
    template<typename... Args>
    class completion_handler_ref_t {
    public:
        constexpr completion_handler_ref_t() : handler() {}

        template<typename Handler,
                 std::enable_if_t<!std::is_same_v<Handler, completion_handler_ref_t<Args...>>, bool> = false>
        // NOLINTNEXTLINE(hicpp-explicit-conversions,bugprone-forwarding-reference-overload) - SFINAE should take care of that :-)
        completion_handler_ref_t(Handler && handler)
            : handler(std::make_unique<completion_handler_t<Handler, Args...>>(std::forward<Handler>(handler)))
        {
        }

        //NOLINTNEXTLINE(hicpp-explicit-conversions)
        completion_handler_ref_t(std::unique_ptr<completion_handler_base_t<Args...>> handler)
            : handler(std::move(handler))
        {
        }

        [[nodiscard]] constexpr explicit operator bool() const { return handler != nullptr; }

        void operator()(Args... args)
        {
            std::unique_ptr<completion_handler_base_t<Args...>> handler {std::move(this->handler)};
            (*handler)(std::forward<Args>(args)...);
        }

    private:
        std::unique_ptr<completion_handler_base_t<Args...>> handler;
    };

    template<typename... Args, typename Handler>
    constexpr completion_handler_ref_t<Args...> make_handler_ref(Handler && handler)
    {
        return completion_handler_ref_t<Args...> {std::forward<Handler>(handler)};
    }
}
