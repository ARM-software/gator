/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "GatorMain.h"
#include "agents/ext_source/ext_source_agent_main.h"
#include "agents/spawn_agent.h"

#include <string_view>

int main(int argc, char ** argv)
{
    // agent main ?
    if ((argc > 1) && (std::string_view(argv[1]) == agents::agent_id_ext_source)) {
        return agents::ext_agent_main(argv[0], lib::Span<char const * const>(argv + 2, argc - 2));
    }

    return gator_main(argc, argv);
}
