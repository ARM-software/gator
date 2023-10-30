/* Copyright (C) 2023 by Arm Limited. All rights reserved. */
#include "agents/armnn/armnn_agent_main.h"

#include "Logging.h"
#include "agents/agent_environment.h"
#include "agents/armnn/armnn_agent.h"
#include "ipc/raw_ipc_channel_source.h"

namespace agents {

    int armnn_agent_main(char const * /*argv0*/, lib::Span<char const * const> args)
    {
        return start_agent(args, [](auto /*args*/, auto & io, auto & pm, auto ipc_sink, auto ipc_source) {
            // Wrap the create function so we can setup the default UDS listeners
            auto factory = [](auto & io, auto & /*pm*/, auto sink, auto terminator) {
                auto agent = armnn_agent_t::create(io, std::move(sink), std::move(terminator));
                agent->add_all_defaults();

                return agent;
            };

            return agent_environment_t<armnn_agent_t>::create("gator-agent-ann",
                                                              io,
                                                              pm,
                                                              std::move(factory),
                                                              std::move(ipc_sink),
                                                              std::move(ipc_source));
        });
    }
}
