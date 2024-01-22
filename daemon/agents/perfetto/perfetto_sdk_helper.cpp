/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#include "agents/perfetto/perfetto_sdk_helper.h"

#include "Logging.h"
#include "lib/AutoClosingFd.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <fcntl.h>
#include <unistd.h>

namespace agents {

    namespace {
        constexpr std::uint32_t perfetto_buffer_size = 2048;
        constexpr std::uint32_t perfetto_flush_period = 100;
        constexpr std::uint32_t perfetto_file_write_period = 100;

        std::optional<std::array<lib::AutoClosingFd, 2>> create_perfetto_pipe()
        {
            std::array<int, 2> fds;
            if (auto result = ::pipe2(fds.data(), O_CLOEXEC); result != 0) {
                LOG_ERROR("Failed to open perfetto data pipe. Result code was: %d", result);
                return {};
            }

            return {{lib::AutoClosingFd(fds[0]), lib::AutoClosingFd(fds[1])}};
        }
    }

    void perfetto_sdk_helper_t::initialize_sdk()
    {
        if (!tracing_session) {
            //Intializing perfetto SDK
            LOG_TRACE("Initializing perfetto SDK");
            perfetto::TracingInitArgs tracing_init_args;
            tracing_init_args.backends = perfetto::kSystemBackend;
            perfetto::Tracing::Initialize(tracing_init_args);

            tracing_session = perfetto::Tracing::NewTrace();
        }
        else {
            LOG_WARNING("Perfetto SDK should be initialized only one time");
        }
    }

    void perfetto_sdk_helper_t::stop_sdk()
    {
        if (tracing_session && session_started) {
            tracing_session->StopBlocking();
            session_started = false;
        }
    }

    bool perfetto_sdk_helper_t::start_trace()
    {
        if (!tracing_session) {
            LOG_ERROR("Attempted to start a perfetto trace but the SDK has not been initialised");
            return false;
        }

        if (session_started) {
            LOG_ERROR("Attempted to start a perfetto trace but another is already in progress");
            return false;
        }

        auto pipe_fds = create_perfetto_pipe();
        if (!pipe_fds) {
            return false;
        }

        fill_trace_configuration();

        // we need to own this even though we hand the write fd to perfetto as it won't take ownership
        // and close it for us.
        perfetto_write_fd = std::move(pipe_fds.value()[1]);
        // adapt the read end to something that asio can deal with
        perfetto_read_stream = boost::asio::posix::stream_descriptor {ctx, pipe_fds.value()[0].release()};

        // now tell perfetto to configure itself and start tracing
        tracing_session->Setup(trace_config, perfetto_write_fd.get());
        tracing_session->Start();

        session_started = true;
        return true;
    }

    void perfetto_sdk_helper_t::fill_trace_configuration()
    {
        trace_config.add_buffers()->set_size_kb(perfetto_buffer_size);
        trace_config.set_flush_period_ms(perfetto_flush_period);
        trace_config.set_file_write_period_ms(perfetto_file_write_period);
        trace_config.set_write_into_file(true);

        auto * data_source = trace_config.add_data_sources()->mutable_config();
        data_source->set_name(std::string(GPU_RENDERSTAGES_DATASOURCE));
    }
}
