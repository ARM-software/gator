/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/proc/process_state.hpp"
#include "async/proc/process_state_tracker.hpp"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"
#include "lib/source_location.h"

#include <atomic>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <variant>

#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>

#include <unistd.h>

namespace async::proc {

    /**
     * Event data for some change in some monitored process
     */
    struct process_monitor_event_t {
        process_uid_t uid;
        pid_t pid;
        ptrace_event_type_t type;
        ptrace_process_state_t state;
        ptrace_process_origin_t origin;
        int status;

        constexpr process_monitor_event_t(process_uid_t uid,
                                          pid_t pid,
                                          ptrace_event_type_t type,
                                          ptrace_process_state_t state,
                                          ptrace_process_origin_t origin,
                                          int status) noexcept
            : uid(uid), pid(pid), type(type), state(state), origin(origin), status(status)
        {
        }
    };

    /**
     * A class designed to ptrace one or more pid, tracking their lifecycle (attach, clone/exec/fork/vfork, exit) and that of subsequent children.
     * Lifecycle events are queued into a set of ptrace_monitor_queue_t objects as they happen.
     */
    class process_monitor_t {
    public:
        struct fork_result_t {
            process_uid_t uid;
            lib::forked_process_t process;
        };

        /** Constructor */
        explicit process_monitor_t(boost::asio::io_context & context) : strand(context) {}

        /** Wait for the next asynchronous event */
        template<typename CompletionToken>
        auto async_wait_event(process_uid_t uid, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code, process_monitor_event_t)>(
                [this, uid](auto && stored_continuation) {
                    submit(start_on(strand) //
                               | then([this, uid, sc = stored_continuation.move()]() mutable {
                                     do_async_wait_event(uid, std::move(sc));
                                 }),
                           stored_continuation.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

        /** Wait for the case where all tracked processes exit */
        template<typename CompletionToken>
        auto async_wait_all_terminated(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [this](auto && stored_continuation) {
                    submit(start_on(strand) //
                               | then([this, sc = stored_continuation.move()]() mutable {
                                     do_async_wait_all_terminated(std::move(sc));
                                 }),
                           stored_continuation.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

        /** Fork/Exec a new child process */
        template<typename CompletionToken>
        auto async_fork_exec(bool prepend_command,
                             std::string cmd,
                             std::vector<std::string> args,
                             boost::filesystem::path cwd,
                             std::optional<std::pair<uid_t, gid_t>> const & uid_gid,
                             lib::stdio_fds_t stdio_fds,
                             CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [this,
                 prepend_command,
                 cmd = std::move(cmd),
                 args = std::move(args),
                 cwd = std::move(cwd),
                 uid_gid = uid_gid,
                 fds = std::move(stdio_fds)]() mutable {
                    return start_on(strand) //
                         | then([this,
                                 prepend_command,
                                 cmd = std::move(cmd),
                                 args = std::move(args),
                                 cwd = std::move(cwd),
                                 uid_gid,
                                 fds = std::move(fds)]() mutable {
                               return do_async_fork_exec(prepend_command, cmd, args, cwd, uid_gid, std::move(fds));
                           }) //
                         | unpack_tuple();
                },
                std::forward<CompletionToken>(token));
        }

        /** Monitor an externally forked process */
        template<typename CompletionToken>
        auto async_monitor_forked_pid(pid_t pid, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [this, pid]() mutable {
                    return start_on(strand) //
                         | then([this, pid]() mutable { return do_async_monitor_forked_pid(pid); });
                },
                std::forward<CompletionToken>(token));
        }

        /** Notify of a SIGCHLD event */
        void on_sigchild();

        /** Abort all pending events, stop listening for new ones */
        void cancel();

    private:
        using process_event_continuation_t =
            async::continuations::stored_continuation_t<boost::system::error_code, process_monitor_event_t>;
        using error_continuation_t = async::continuations::stored_continuation_t<boost::system::error_code>;

        struct process_monitor_metadata_t {
            std::deque<process_monitor_event_t> queued_events {};
            process_event_continuation_t queued_handler {};
        };

        using process_tracker_t = process_state_tracker_t<process_monitor_t, process_monitor_metadata_t>;

        template<typename T>
        using error_and_t = std::pair<boost::system::error_code, T>;
        using error_event_tracker_t = lib::error_code_or_t<process_tracker_t *, error_and_t<process_monitor_event_t>>;

        friend process_tracker_t;

        template<typename Handler>
        static constexpr error_and_t<process_monitor_event_t> unpack_error(error_event_tracker_t && error_or_pt,
                                                                           Handler && handler)
        {
            auto const * error = lib::get_error(error_or_pt);
            if (error != nullptr) {
                return *error;
            }
            return handler(lib::get_value(error_or_pt));
        }

        boost::asio::io_context::strand strand;
        error_continuation_t all_terminated_handler {};
        std::map<process_uid_t, process_tracker_t> process_states {};
        std::uint64_t uid_counter {0};
        bool all_terminated_flag {false};

        // create / get helpers
        error_event_tracker_t await_get_common(process_uid_t uid);

        // async_xxx handlers
        [[nodiscard]] error_and_t<fork_result_t> do_async_fork_exec(
            bool prepend_command,
            std::string const & cmd,
            std::vector<std::string> const & args,
            boost::filesystem::path const & cwd,
            std::optional<std::pair<uid_t, gid_t>> const & uid_gid,
            lib::stdio_fds_t stdio_fds);
        [[nodiscard]] process_uid_t do_async_monitor_forked_pid(pid_t pid);
        void do_async_wait_event(process_uid_t uid, process_event_continuation_t handler);
        void do_async_wait_all_terminated(error_continuation_t handler);

        // single handling
        [[nodiscard]] bool do_waitpid_for(process_tracker_t & process_tracker);

        // callbacks for process_state_tracker_t
        [[nodiscard]] bool on_process_state_changed(process_tracker_t & pt);

        // event processing
        void queue_event(process_tracker_t & pt,
                         process_uid_t uid,
                         pid_t pid,
                         ptrace_event_type_t type,
                         ptrace_process_state_t state,
                         ptrace_process_origin_t origin,
                         int status);
        [[nodiscard]] bool flush_events(process_tracker_t & pt);

        void do_check_all_terminated();
    };
}
