/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "Config.h"
#include "GatorMain.h"
#include "agents/ext_source/ext_source_agent_main.h"
#include "agents/perf/perf_agent_main.h"
#include "agents/spawn_agent.h"
#include "lib/Span.h"

#if CONFIG_ARMNN_AGENT
#include "agents/armnn/armnn_agent_main.h"
#endif

#ifdef CONFIG_USE_PERFETTO
#include "agents/perfetto/perfetto_agent_main.h"
#endif

#include <csignal>
#include <cstdlib>
#include <string>
#include <string_view>

/// If requested by the user (via env var), pause the current process, awaiting a debugger.
void hold_for_debug(std::string_view agent_name)
{
    std::string env_name = "DEBUG_HOLD_AGENT_";
    env_name += agent_name;

    if (std::getenv(env_name.c_str()) != nullptr) { // NOLINT(concurrency-mt-unsafe)
        (void) raise(SIGSTOP);
    }
}

int main(int argc, char ** argv)
{
    // agent main ?
    if (argc > 1) {
        if (std::string_view(argv[1]) == agents::agent_id_ext_source) {
            hold_for_debug("EXTERNAL");
            return agents::ext_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
        if (std::string_view(argv[1]) == agents::agent_id_perf) {
            hold_for_debug("PERF");
            return agents::perf::perf_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
#if CONFIG_ARMNN_AGENT
        if (std::string_view(argv[1]) == agents::agent_id_armnn) {
            hold_for_debug("ARMNN");
            return agents::armnn_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
#endif
#ifdef CONFIG_USE_PERFETTO
        if (std::string_view(argv[1]) == agents::agent_id_perfetto) {
            hold_for_debug("PERFETTO");
            return agents::perfetto_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
#endif
    }

    return gator_main(argc, argv);
}
