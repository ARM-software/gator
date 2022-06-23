/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <type_traits>

#include <boost/asio/execution.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/is_executor.hpp>

namespace async {
    /** Helper trait to check if some type is an executor type */
    template<typename Executor>
    constexpr bool is_asio_executor_v =
        boost::asio::execution::is_executor<Executor>::value || boost::asio::is_executor<Executor>::value;

    /** Helper trait to check if some type is an execution context */
    template<typename ExecutionContext>
    constexpr bool is_asio_execution_context_v =
        std::is_convertible_v<ExecutionContext &, boost::asio::execution_context &>;
}
