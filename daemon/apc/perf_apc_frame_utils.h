/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Buffer.h"
#include "Protocol.h"
#include "Time.h"
#include "agents/perf/async_buffer_builder.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/Span.h"
#include "perf_counter.h"

#include <string_view>
#include <vector>

namespace apc {

    namespace detail {

        inline void make_perf_attr_frame_header(CodeType type,
                                                agents::perf::apc_buffer_builder_t<std::vector<char>> & buffer)
        {
            buffer.packInt(static_cast<int32_t>(FrameType::PERF_ATTRS));
            buffer.packInt(0); // legacy, used to be core number
            buffer.packInt(static_cast<int32_t>(type));
        }

        inline void write_string_view(std::string_view sv,
                                      agents::perf::apc_buffer_builder_t<std::vector<char>> & buffer)
        {
            buffer.writeBytes(sv.data(), sv.size());
            bool string_is_not_null_terminated = !(sv.back() == 0);
            if (string_is_not_null_terminated) {
                buffer.packInt(0);
            }
        }

        [[nodiscard]] inline std::vector<char> make_cpu_frame(CodeType type, monotonic_delta_t timestamp, int32_t cpu)
        {
            std::vector<char> frame {};
            agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
            detail::make_perf_attr_frame_header(type, buffer);
            buffer.packMonotonicDelta(timestamp);
            buffer.packInt(cpu);
            buffer.endFrame();
            return frame;
        }

    }

    [[nodiscard]] inline std::vector<char> make_perf_events_attributes_frame(perf_event_attr const & pea, int key)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::PEA, buffer);
        buffer.writeBytes(reinterpret_cast<const char *>(&pea), pea.size);
        buffer.packInt(key);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_keys_frame(lib::Span<uint64_t const> ids, lib::Span<int const> keys)
    {
        runtime_assert(ids.size() == keys.size(), "expected equal numbers of ids and keys");

        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::KEYS, buffer);

        int count = static_cast<int>(ids.size());
        buffer.packInt(count);
        for (int i = 0; i < count; ++i) {
            buffer.packInt64(static_cast<int64_t>(ids[i]));
            buffer.packInt(keys[i]);
        }
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_old_keys_frame(lib::Span<int const> keys, lib::Span<const char> bytes)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::KEYS_OLD, buffer);

        buffer.packInt(static_cast<int>(keys.size()));
        for (int const key : keys) {
            buffer.packInt(key);
        }
        buffer.writeBytes(bytes.data(), bytes.size());
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_format_frame(std::string_view format)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::FORMAT, buffer);
        detail::write_string_view(format, buffer);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_maps_frame(int pid, int tid, std::string_view maps)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::MAPS, buffer);
        buffer.packInt(pid);
        buffer.packInt(tid);
        detail::write_string_view(maps, buffer);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_comm_frame(int pid,
                                                           int tid,
                                                           std::string_view image,
                                                           std::string_view comm)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::COMM, buffer);

        buffer.packInt(pid);
        buffer.packInt(tid);
        detail::write_string_view(image, buffer);
        detail::write_string_view(comm, buffer);
        buffer.endFrame();

        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_cpu_online_frame(monotonic_delta_t timestamp, int32_t cpu)
    {
        return detail::make_cpu_frame(CodeType::ONLINE_CPU, timestamp, cpu);
    }

    [[nodiscard]] inline std::vector<char> make_cpu_offline_frame(monotonic_delta_t timestamp, int32_t cpu)
    {
        return detail::make_cpu_frame(CodeType::OFFLINE_CPU, timestamp, cpu);
    }

    [[nodiscard]] inline std::vector<char> make_kallsyms_frame(std::string_view kallsyms)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::KALLSYMS, buffer);
        detail::write_string_view(kallsyms, buffer);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_perf_counters_frame(monotonic_delta_t timestamp,
                                                                    lib::Span<perf_counter_t const> counters)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::COUNTERS, buffer);

        buffer.packMonotonicDelta(timestamp);
        for (perf_counter_t pc : counters) {
            buffer.packInt(pc.core);
            buffer.packInt(pc.key);
            buffer.packInt64(pc.value);
        }
        buffer.packInt(-1);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_header_page_frame(std::string_view header_page)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::HEADER_PAGE, buffer);
        detail::write_string_view(header_page, buffer);
        buffer.endFrame();
        return frame;
    }

    [[nodiscard]] inline std::vector<char> make_header_event_frame(std::string_view header_event)
    {
        std::vector<char> frame {};
        agents::perf::apc_buffer_builder_t<std::vector<char>> buffer(frame);
        detail::make_perf_attr_frame_header(CodeType::HEADER_EVENT, buffer);
        detail::write_string_view(header_event, buffer);
        buffer.endFrame();
        return frame;
    }

}
