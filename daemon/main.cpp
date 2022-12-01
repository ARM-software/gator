/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "GatorMain.h"
#include "agents/ext_source/ext_source_agent_main.h"
#include "agents/perf/perf_agent_main.h"
#include "agents/spawn_agent.h"

#if defined(ANDROID) || defined(__ANDROID__)
#include "agents/perfetto/perfetto_agent_main.h"
#endif

#include <string_view>

int main(int argc, char ** argv)
{
    // agent main ?
    if (argc > 1) {
        if (std::string_view(argv[1]) == agents::agent_id_ext_source) {
            return agents::ext_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
        if (std::string_view(argv[1]) == agents::agent_id_perf) {
            return agents::perf::perf_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
#if defined(ANDROID) || defined(__ANDROID__)
        if (std::string_view(argv[1]) == agents::agent_id_perfetto) {
            return agents::perfetto_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
        }
#endif
    }

    return gator_main(argc, argv);
}
