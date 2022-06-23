/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/proc/process_state.hpp"
#include "async/proc/wait.hpp"

#include <cstring>

#include <sys/ptrace.h>

namespace async::proc {

    /**
     * Tracked process state machine
     */
    template<typename Callbacks, typename Metadata>
    class process_state_tracker_t {
    public:
        using callbacks_t = Callbacks;
        using metadata_t = Metadata;

        /**
         * Constructor
         * @param uid The process uid
         * @param pid The process (thread) id
         * @param origin How the process was discovered
         */
        process_state_tracker_t(process_uid_t uid, pid_t pid, ptrace_process_origin_t origin)
            : uid(uid), ppid(0), pid(pid), origin(origin)
        {
        }

        [[nodiscard]] process_uid_t get_uid() const { return uid; }
        [[nodiscard]] pid_t get_ppid() const { return ppid; };
        [[nodiscard]] pid_t get_pid() const { return pid; }
        [[nodiscard]] ptrace_process_origin_t get_origin() const { return origin; }
        [[nodiscard]] ptrace_process_state_t get_state() const { return state; }
        [[nodiscard]] int get_status_code() { return status_code; }
        [[nodiscard]] metadata_t const & get_metadata() const { return metadata; }
        [[nodiscard]] metadata_t & get_metadata() { return metadata; }

        /**
         * Process the next 'status' value received from waitpid for this process
         *
         * @param status The status value from waitpid
         * @param callbacks The object used to manipulate external state (such as ptrace, track new process etc)
         */
        void process_wait_status(unsigned status, callbacks_t & callbacks)
        {
            // for debugging
            LOG_TRACE("PID[%d] received wait status update (status = 0x%x)", pid, status);

            // and exit events
            if (w_if_exited(status)) {
                return on_process_exited(w_exit_status(status), callbacks);
            }

            // and signal termination events
            if (w_if_signaled(status)) {
                return on_process_signaled(w_term_sig(status), callbacks);
            }
        }

        /** Called on successful fork completion */
        void process_fork_complete(callbacks_t & callbacks)
        {
            if (origin != ptrace_process_origin_t::forked) {
                LOG_DEBUG("PID[%d] Unexpected origin for fork complete", pid);
                return;
            }
            if (state == ptrace_process_state_t::attaching) {
                transition_state(ptrace_process_state_t::attached, 0, callbacks);
            }
        }

        /** Called when waitpid returns ECHILD */
        void on_waitpid_echild(callbacks_t & callbacks)
        {
            // just assume it exited ok, if not already exited
            if ((state != ptrace_process_state_t::terminated_exit)
                && (state != ptrace_process_state_t::terminated_signal)) {
                transition_state(ptrace_process_state_t::terminated_exit, 0, callbacks);
            }
        }

    private:
        process_uid_t uid;
        pid_t ppid;
        pid_t pid;
        int status_code;
        ptrace_process_origin_t origin;
        ptrace_process_state_t state {ptrace_process_state_t::attaching};
        metadata_t metadata {};

        void on_process_exited(int exit_status, callbacks_t & callbacks)
        {
            LOG_DEBUG("PID[%d] exited with status code %d", pid, exit_status);

            transition_state(ptrace_process_state_t::terminated_exit, exit_status, callbacks);
        }

        void on_process_signaled(int signo, callbacks_t & callbacks)
        {
            LOG_DEBUG("PID[%d] exited with signal %d (%s)",
                      pid,
                      signo,
                      strsignal(signo)); // NOLINT(concurrency-mt-unsafe)

            transition_state(ptrace_process_state_t::terminated_signal, signo, callbacks);
        }

        void transition_state(ptrace_process_state_t to_state, int status, callbacks_t & callbacks)
        {
            if (state == to_state) {
                // no state change; do nothing
                return;
            }

            // move to the new state
            LOG_TRACE("PID[%d] transitioned from %s to %s", pid, to_cstring(state), to_cstring(to_state));

            // update our state
            state = to_state;
            status_code = status;

            // notify the callbacks
            (void) callbacks.on_process_state_changed(*this);
        }
    };
}
