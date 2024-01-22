/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#include "agents/perfetto/perfetto_agent_main.h"

#include "agents/agent_environment.h"
#include "agents/perfetto/perfetto_agent.h"
#include "agents/perfetto/perfetto_sdk_helper.h"
#include "lib/Span.h"

namespace agents {

    int perfetto_agent_main(char const * /*argv0*/, lib::Span<char const * const> args)
    {
        return start_agent(args, [](auto /*args*/, auto & io, auto & pm, auto ipc_sink, auto ipc_source) {
            auto factory = [](auto & io, auto & /*pm*/, auto sink, auto terminator) {
                auto agent = perfetto_agent_t<>::create(io,
                                                        std::move(sink),
                                                        std::move(terminator),
                                                        std::make_shared<agents::perfetto_sdk_helper_t>(io));
                return agent;
            };

            return agent_environment_t<perfetto_agent_t<>>::create("gator-agent-pfto",
                                                                   io,
                                                                   pm,
                                                                   std::move(factory),
                                                                   std::move(ipc_sink),
                                                                   std::move(ipc_source));
        });
    }
};
