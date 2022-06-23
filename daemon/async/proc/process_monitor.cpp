/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#include "async/proc/process_monitor.hpp"

#include "Logging.h"
#include "async/continuations/operations.h"
#include "async/proc/process_state.hpp"
#include "async/proc/process_state_tracker.hpp"
#include "lib/Assert.h"
#include "lib/error_code_or.hpp"
#include "lib/forked_process.h"

#include <cerrno>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <sched.h>

namespace async::proc {
    process_monitor_t::error_event_tracker_t process_monitor_t::await_get_common(process_uid_t uid)
    {
        // find the process
        auto it = process_states.find(uid);
        if (it == process_states.end()) {
            // send terminated error if it is not known
            LOG_TRACE("UID[%" PRIu64 "] Cannot find UID", std::uint64_t(uid));
            return error_and_t<process_monitor_event_t> {
                boost::system::errc::make_error_code(boost::system::errc::no_such_process),
                process_monitor_event_t(uid,
                                        0,
                                        ptrace_event_type_t::error,
                                        ptrace_process_state_t::no_such_process,
                                        ptrace_process_origin_t::forked,
                                        0),
            };
        }
        return &(it->second);
    }

    void process_monitor_t::do_async_wait_event(process_uid_t uid, process_event_continuation_t handler)
    {
        // get the already tracked item
        auto const pt_or_error = await_get_common(uid);

        auto const * error = lib::get_error(pt_or_error);
        if (error != nullptr) {
            resume_continuation(strand.context(), std::move(handler), error->first, error->second);
            return;
        }

        auto * const pt = lib::get_value(pt_or_error);

        runtime_assert(pt != nullptr, "pt must not be nullptr");

        // is there a current handler ? cancel it with the current state
        auto & metadata = pt->get_metadata();
        if (metadata.queued_handler) {
            LOG_TRACE("UID[%" PRIu64 "] Terminating old handler", std::uint64_t(uid));
            resume_continuation(strand.context(),
                                std::move(metadata.queued_handler),
                                boost::system::errc::make_error_code(boost::system::errc::operation_canceled),
                                process_monitor_event_t(uid,
                                                        pt->get_pid(),
                                                        ptrace_event_type_t::error,
                                                        pt->get_state(),
                                                        pt->get_origin(),
                                                        pt->get_status_code()));
        }

        LOG_TRACE("UID[%" PRIu64 "] Saving new handler", std::uint64_t(uid));

        // store the new handler
        metadata.queued_handler = std::move(handler);

        // check / flush a queued event
        if (flush_events(*pt)) {
            process_states.erase(uid);
            do_check_all_terminated();
        }
    }

    void process_monitor_t::do_async_wait_all_terminated(error_continuation_t handler)
    {
        // abort the old handler, if it was set
        error_continuation_t old_handler {std::move(all_terminated_handler)};
        if (old_handler) {
            resume_continuation(strand.context(),
                                std::move(old_handler),
                                boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
        }

        // process existing flag
        if (all_terminated_flag) {
            // reset the flag
            all_terminated_flag = false;

            // invoke the new handler
            resume_continuation(strand.context(), std::move(handler), {});
        }
        // save the new handler for later
        else {
            all_terminated_handler = std::move(handler);
        }
    }

    void process_monitor_t::cancel()
    {
        // abort / erase our tracked process state since the child will not be ptracing and will not have any known children (at this point)
        for (auto & entry : process_states) {
            process_event_continuation_t handler {std::move(entry.second.get_metadata().queued_handler)};
            if (handler) {
                resume_continuation(strand.context(),
                                    std::move(handler),
                                    boost::system::errc::make_error_code(boost::system::errc::operation_canceled),
                                    process_monitor_event_t(entry.first,
                                                            entry.second.get_pid(),
                                                            ptrace_event_type_t::error,
                                                            entry.second.get_state(),
                                                            entry.second.get_origin(),
                                                            entry.second.get_status_code()));
            }
        }

        if (all_terminated_handler) {
            resume_continuation(strand.context(),
                                std::move(all_terminated_handler),
                                boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
        }
    }

    process_monitor_t::error_and_t<process_monitor_t::fork_result_t> process_monitor_t::do_async_fork_exec(
        bool prepend_command,
        std::string const & cmd,
        std::vector<std::string> const & args,
        boost::filesystem::path const & cwd,
        std::optional<std::pair<uid_t, gid_t>> const & uid_gid,
        lib::stdio_fds_t stdio_fds)
    {
        // fork the process and check for any errors
        auto result =
            lib::forked_process_t::fork_process(prepend_command, cmd, args, cwd, uid_gid, std::move(stdio_fds));
        auto const * error = lib::get_error(result);
        if (error != nullptr) {
            return {*error, fork_result_t {}};
        }

        // the process must have forked successfully
        auto & forked_process = lib::get_value(result);
        runtime_assert(!!forked_process, "expected valid forked process");
        auto const pid = forked_process.get_pid();
        auto const uid = process_uid_t(uid_counter++);

        // insert the entry into the process table
        auto [it, inserted] =
            process_states.insert_or_assign(uid, process_tracker_t(uid, pid, ptrace_process_origin_t::forked));

        runtime_assert(inserted, "expected uid to be unique");

        // update state
        it->second.process_fork_complete(*this);

        // check / flush a queued event
        if (flush_events(it->second)) {
            process_states.erase(it);
            do_check_all_terminated();
        }

        return {boost::system::error_code {}, fork_result_t {uid, std::move(forked_process)}};
    }

    process_uid_t process_monitor_t::do_async_monitor_forked_pid(pid_t pid)
    {
        auto const uid = process_uid_t(uid_counter++);

        // insert the entry into the process table
        auto [it, inserted] =
            process_states.insert_or_assign(uid, process_tracker_t(uid, pid, ptrace_process_origin_t::forked));

        runtime_assert(inserted, "expected uid to be unique");

        // update state
        it->second.process_fork_complete(*this);

        // check / flush a queued event
        if (flush_events(it->second)) {
            process_states.erase(it);
            do_check_all_terminated();
        }

        return uid;
    }

    /** Handle the sigchld event */
    void process_monitor_t::on_sigchild()
    {
        using namespace async::continuations;

        // iterate each child agent and check if it terminated.
        // if so, notify its worker and remove it from the map.
        //
        // We don't use waitpid(0 or -1, ...) since there are other waitpid calls that block on a single uid and we dont
        // want to swallow the process event from them
        spawn("SIGCHLD handler",
              start_on(strand) //
                  | then([this]() {
                        // ignore if nothing monitored
                        if (process_states.empty()) {
                            return;
                        }

                        // check all the child processes
                        for (auto it = process_states.begin(); it != process_states.end();) {
                            if (do_waitpid_for(it->second)) {
                                it = process_states.erase(it);
                            }
                            else {
                                ++it;
                            }
                        }

                        // stop if no more items
                        do_check_all_terminated();
                    }));
    }

    /** Check the exit status for some worker process */
    bool process_monitor_t::do_waitpid_for(process_tracker_t & process_tracker)
    {
        int wstatus = 0;

        while (true) {
            auto const result = lib::waitpid(process_tracker.get_pid(), &wstatus, WNOHANG);
            auto const error = errno;

            // no change?
            if (result == 0) {
                return false;
            }

            // error?
            if (result == pid_t(-1)) {
                // ignore agains
                if ((error == EINTR) || (error == EAGAIN) || (error == EWOULDBLOCK)) {
                    continue;
                }

                // report other errors
                if (error != ECHILD) {
                    LOG_DEBUG("waitpid reports uid=%" PRIu64 " unexpected error %d",
                              std::uint64_t(process_tracker.get_uid()),
                              error);
                }
                else {
                    LOG_DEBUG("waitpid reports uid=%" PRIu64 " is terminated",
                              std::uint64_t(process_tracker.get_uid()));
                }

                // process the status
                process_tracker.on_waitpid_echild(*this);
            }
            else {
                // process the status
                LOG_TRACE("Got waitpid(result=%d, wstatus=%d, pid=%d, uid=%" PRIu64 ")",
                          result,
                          wstatus,
                          process_tracker.get_pid(),
                          std::uint64_t(process_tracker.get_uid()));
                process_tracker.process_wait_status(wstatus, *this);
            }

            return flush_events(process_tracker);
        }
    }

    bool process_monitor_t::on_process_state_changed(process_tracker_t & pt)
    {
        queue_event(pt,
                    pt.get_uid(),
                    pt.get_pid(),
                    ptrace_event_type_t::state_change,
                    pt.get_state(),
                    pt.get_origin(),
                    pt.get_status_code());

        return true;
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void process_monitor_t::queue_event(process_tracker_t & pt,
                                        process_uid_t uid,
                                        pid_t pid,
                                        ptrace_event_type_t type,
                                        ptrace_process_state_t state,
                                        ptrace_process_origin_t origin,
                                        int status)
    {
        pt.get_metadata().queued_events.emplace_back(uid, pid, type, state, origin, status);
    }

    bool process_monitor_t::flush_events(process_tracker_t & pt)
    {
        LOG_TRACE("UID[%" PRIu64 "] flushing event queue...", std::uint64_t(pt.get_uid()));

        auto & metadata = pt.get_metadata();

        // the caller should be holding the mutex lock on entry
        if ((!metadata.queued_events.empty()) && metadata.queued_handler) {
            LOG_TRACE("UID[%" PRIu64 "] triggering one event handler...", std::uint64_t(pt.get_uid()));

            // remove just the head event
            auto event = metadata.queued_events.front();
            metadata.queued_events.pop_front();

            // move the queued_handler into a local copy, clearing the one in pt and invoke the handler
            resume_continuation(strand.context(), std::move(metadata.queued_handler), {}, event);

            // are there any events left on a terminated process?
            if (metadata.queued_events.empty()
                && ((pt.get_state() == ptrace_process_state_t::terminated_exit)
                    || (pt.get_state() == ptrace_process_state_t::terminated_signal))) {
                LOG_TRACE("UID[%" PRIu64 "] is terminated and has no pending events", std::uint64_t(pt.get_uid()));

                return true;
            }

            if (logging::is_log_enable_trace()) {
                LOG_TRACE("The following pids are still tracked: ");
                for (auto const & entry : process_states) {
                    LOG_TRACE("... UID[%" PRIu64 "] {ppid=%d, pid=%d, state=%s, origin=%s}",
                              std::uint64_t(entry.second.get_uid()),
                              entry.second.get_ppid(),
                              entry.second.get_pid(),
                              to_cstring(entry.second.get_state()),
                              to_cstring(entry.second.get_origin()));
                }
            }
        }

        return false;
    }

    void process_monitor_t::do_check_all_terminated()
    {
        // send all-terminated message?
        if (!process_states.empty()) {
            return;
        }

        LOG_TRACE("All traced processes are gone");

        // move out the existing handler
        error_continuation_t all_terminated_handler {std::move(this->all_terminated_handler)};

        if (all_terminated_handler) {
            // reset the flag
            all_terminated_flag = false;

            // call the handler
            resume_continuation(strand.context(), std::move(all_terminated_handler), {});
        }
        else {
            // set the flag for later
            all_terminated_flag = true;
        }
    }
}
