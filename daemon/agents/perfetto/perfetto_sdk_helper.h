/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "Logging.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "lib/AutoClosingFd.h"
#include "lib/Utils.h"
#include "perfetto/sdk/perfetto.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/system/error_code.hpp>

namespace agents {
    /**
    * A handler for the connection between Perfetto SDK and perfetto agent and a wrapper for the SDK functions
    */

    class perfetto_sdk_helper_t : public std::enable_shared_from_this<perfetto_sdk_helper_t> {

    public:
        using data_source_list_t = std::unordered_map<int, std::vector<std::string>>;

        explicit perfetto_sdk_helper_t(boost::asio::io_context & context) : ctx(context), strand(context) {}

        void initialize_sdk();

        void stop_sdk();

        bool start_trace();

        template<typename CompletionToken>
        [[nodiscard]] auto async_read_trace(boost::asio::mutable_buffer buffer, CompletionToken && token)
        {
            using namespace async::continuations;

            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, std::size_t)>(
                [self = this->shared_from_this()](auto && handler, auto buffer) {
                    using handler_type = decltype(handler);

                    if (!self->session_started) {
                        auto err = boost::asio::error::make_error_code(boost::asio::error::not_connected);
                        handler(err, 0);
                    }

                    self->perfetto_read_stream.value().async_read_some(buffer, std::forward<handler_type>(handler));
                },
                std::forward<CompletionToken>(token),
                buffer);
        }

    private:
        static constexpr std::string_view GPU_RENDERSTAGES_DATASOURCE = "gpu.renderstages";

        boost::asio::io_context & ctx;
        boost::asio::io_context::strand strand;

        std::unique_ptr<perfetto::TracingSession> tracing_session = nullptr;
        perfetto::TraceConfig trace_config;
        bool session_started {false};

        lib::AutoClosingFd perfetto_write_fd;
        std::optional<boost::asio::posix::stream_descriptor> perfetto_read_stream;

        void fill_trace_configuration();
    };
};
