/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/async_buffer_builder.h"
#include "agents/perf/perf_driver_summary.h"
#include "lib/String.h"

#include <string_view>
#include <vector>

#include <Protocol.h>

namespace apc {

    namespace detail {
        inline void make_summary_frame_header(MessageType type,
                                              agents::perf::apc_buffer_builder_t<std::vector<char>> & buffer)
        {
            buffer.packInt(static_cast<int32_t>(FrameType::SUMMARY));
            buffer.packInt(static_cast<int32_t>(type));
        }
    }

    [[nodiscard]] inline std::vector<char> make_summary_message(agents::perf::perf_driver_summary_state_t const & state)
    {
        constexpr std::size_t max_page_size_chars = 32;

        lib::printf_str_t<max_page_size_chars> page_size_str {"%li", state.page_size};

        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> builder {frame};

        detail::make_summary_frame_header(MessageType::SUMMARY, builder);

        builder.writeString(NEWLINE_CANARY);
        builder.packInt64(state.clock_realtime);
        builder.packInt64(state.clock_boottime);
        builder.packInt64(state.clock_monotonic_raw);
        builder.writeString("uname");
        builder.writeString(state.uname.c_str());
        builder.writeString("PAGESIZE");
        builder.writeString(page_size_str.c_str());
        if (state.nosync) {
            builder.writeString("nosync");
            builder.writeString("");
        }
        for (const auto & pair : state.additional_attributes) {
            if (!pair.first.empty()) {
                builder.writeString(pair.first.c_str());
                builder.writeString(pair.second.c_str());
            }
        }
        builder.writeString("");
        builder.endFrame();

        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_core_name_message(int core, int cpuid, std::string_view name)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> builder {frame};

        detail::make_summary_frame_header(MessageType::CORE_NAME, builder);

        builder.packInt(core);
        builder.packInt(cpuid);
        builder.writeString(name);
        builder.endFrame();

        return frame;
    }
}
