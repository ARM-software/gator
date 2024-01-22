/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "agents/agent_environment.h"
#include "agents/perf/perf_agent.h"
#include "agents/perf/perf_capture.h"
#include "async/proc/process_monitor.hpp"
#include "ipc/raw_ipc_channel_sink.h"
#include "lib/Span.h"

#include <memory>
#include <utility>

#include <boost/asio/io_context.hpp>

namespace agents::perf {

    namespace {
        using agent_type = perf_agent_t<perf_capture_t>;

        auto agent_factory(boost::asio::io_context & io,
                           async::proc::process_monitor_t & process_monitor,
                           std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink,
                           agent_environment_base_t::terminator terminator)
        {
            return agent_type::create(io,
                                      process_monitor,
                                      std::move(sink),
                                      std::move(terminator),
                                      perf_capture_t::create);
        };
    }

    int perf_agent_main(char const * /*argv0*/, lib::Span<const char * const> args)
    {
        return start_agent(args, [](auto /*args*/, auto & io, auto & pm, auto ipc_sink, auto ipc_source) {
            return agent_environment_t<agent_type>::create("gator-agent-perf",
                                                           io,
                                                           pm,
                                                           agent_factory,
                                                           std::move(ipc_sink),
                                                           std::move(ipc_source));
        });
    }
}
