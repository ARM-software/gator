/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "async/proc/async_process.hpp"

#include "Logging.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/process_monitor.hpp"
#include "async/proc/process_state.hpp"

#include <memory>
#include <utility>

#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

namespace async::proc {
    void async_process_t::on_output_complete(boost::system::error_code const & ec, bool is_stderr)
    {
        using namespace async::continuations;

        spawn("async process set complete",
              start_on(strand) //
                  | then([ec, is_stderr, st = shared_from_this()]() {
                        // log the error
                        if (ec) {
                            st->notify(ec);
                        }

                        // update the state
                        if (is_stderr) {
                            st->stderr_complete = true;
                        }
                        else {
                            st->stdout_complete = true;
                        }

                        // flush any events
                        st->flush();
                    }));
    }

    void async_process_t::start()
    {
        using namespace async::continuations;

        auto st = shared_from_this();

        // observe events
        spawn("async_process_t event loop",
              repeatedly(
                  [st]() {
                      return start_on(st->strand) //
                           | then([st]() { return !st->already_terminated; });
                  }, //
                  [st]() {
                      return st->process_monitor.async_wait_event(st->uid, use_continuation) //
                           | post_on(st->strand)                                             //
                           | then([st](boost::system::error_code const & ec,
                                       async::proc::process_monitor_event_t const & event) {
                                 st->process_event(ec, event);
                             });
                  }),
              [st](bool failed, boost::system::error_code ec) {
                  if (failed) {
                      // store the failure
                      spawn("failure notifier",
                            start_on(st->strand) //
                                | then([st, ec]() {
                                      if (ec) {
                                          st->notify(ec);
                                      }
                                      else {
                                          st->notify(boost::system::errc::make_error_code(
                                              boost::system::errc::state_not_recoverable));
                                      }
                                  }));
                  }
              });
    }

    void async_process_t::do_async_wait_complete(completion_handler_t sc)
    {
        // cancel any already pending handler - only one may be queued at a time
        completion_handler_t existing {std::move(completion_handler)};
        if (existing) {
            resume_continuation(strand.context(),
                                std::move(existing),
                                boost::system::errc::make_error_code(boost::system::errc::operation_canceled),
                                false,
                                0);
        }

        // has it already terminated?
        if (already_terminated && stdout_complete && stderr_complete) {
            resume_continuation(strand.context(), std::move(sc), {}, terminated_by_signal, exit_status);

            return;
        }

        // ok, store it for later
        completion_handler = std::move(sc);
    }

    void async_process_t::process_event(boost::system::error_code const & ec,
                                        async::proc::process_monitor_event_t const & event)
    {
        if (ec) {
            notify(ec);
        }

        switch (event.state) {
            case async::proc::ptrace_process_state_t::terminated_exit: {
                terminate(false, event.status);
                return;
            }
            case async::proc::ptrace_process_state_t::terminated_signal: {
                terminate(true, event.status);
                return;
            }
            case async::proc::ptrace_process_state_t::attached:
            case async::proc::ptrace_process_state_t::attaching:
            case async::proc::ptrace_process_state_t::no_such_process:
            default: {
                LOG_TRACE("ignoring unexpected event state %s::%s", to_cstring(event.type), to_cstring(event.state));
                return;
            }
        }
    }

    void async_process_t::notify(boost::system::error_code const & ec)
    {
        if (!already_terminated) {
            pending_errors.emplace_back(ec);
            flush();
        }
    }

    void async_process_t::terminate(bool by_signal, int status)
    {
        if (!already_terminated) {
            terminated_by_signal = by_signal;
            exit_status = status;
            already_terminated = true;
            flush();
        }
    }

    void async_process_t::flush()
    {
        auto const really_terminated = (already_terminated && stdout_complete && stderr_complete);

        if (pending_errors.empty() && !really_terminated) {
            return;
        }

        completion_handler_t existing {std::move(completion_handler)};
        if (!existing) {
            return;
        }

        if (!pending_errors.empty()) {
            auto error = pending_errors.front();
            pending_errors.pop_front();

            resume_continuation(strand.context(), std::move(existing), error, false, 0);

            return;
        }

        resume_continuation(strand.context(), std::move(existing), {}, terminated_by_signal, exit_status);
    }

}
