/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */
#include "agents/ext_source/ext_source_agent_main.h"

#include "agents/agent_environment.h"
#include "agents/ext_source/ext_source_agent.h"
#include "lib/Span.h"

namespace agents {

    int ext_agent_main(char const * /*argv0*/, lib::Span<char const * const> args)
    {
        return start_agent(args, [](auto /*args*/, auto & io, auto & pm, auto ipc_sink, auto ipc_source) {
            // Wrap the create function so we can setup the default UDS and TCP listeners
            auto factory = [](auto & io, auto & /*pm*/, auto sink, auto terminator) {
                auto agent = ext_source_agent_t::create(io, std::move(sink), std::move(terminator));
                agent->add_all_defaults();

                return agent;
            };

            return agent_environment_t<ext_source_agent_t>::create("gator-agent-xs",
                                                                   io,
                                                                   pm,
                                                                   std::move(factory),
                                                                   std::move(ipc_sink),
                                                                   std::move(ipc_source));
        });
    }
}
