/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>

namespace async::proc {
    /** Used to uniquely identify a process in case of pid-reuse */
    enum class process_uid_t : std::uint64_t;

    /**
     * Enumerates the possible event types
     */
    enum class ptrace_event_type_t {
        state_change,
        error,
    };

    /**
     * Enumerates the possible traced process states
     */
    enum class ptrace_process_state_t {
        attaching = 0,
        attached,
        terminated_exit,
        terminated_signal,
        no_such_process,
    };

    /**
     * Enumerates how the process was discovered
     */
    enum class ptrace_process_origin_t {
        /** The process is a subprocess created by 'fork' */
        forked,
    };

    constexpr char const * to_cstring(ptrace_event_type_t state) noexcept
    {
        switch (state) {
            case ptrace_event_type_t::state_change:
                return "state_change";
            case ptrace_event_type_t::error:
                return "error";
            default:
                return "???";
        }
    }

    constexpr char const * to_cstring(ptrace_process_state_t state) noexcept
    {
        switch (state) {
            case ptrace_process_state_t::attaching:
                return "attaching";
            case ptrace_process_state_t::attached:
                return "attached";
            case ptrace_process_state_t::terminated_exit:
                return "terminated_exit";
            case ptrace_process_state_t::terminated_signal:
                return "terminated_signal";
            case ptrace_process_state_t::no_such_process:
                return "no_such_process";
            default:
                return "???";
        }
    }

    constexpr char const * to_cstring(ptrace_process_origin_t state) noexcept
    {
        switch (state) {
            case ptrace_process_origin_t::forked:
                return "forked";
            default:
                return "???";
        }
    }
}
