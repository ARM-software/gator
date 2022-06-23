/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

#include <set>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

namespace async {
    /** Asynchronously polls /proc to find the given command, and returns the PIDs associated with it.
     *  
     * @tparam Executor Executor type
     */
    template<typename Executor>
    class async_wait_for_process_t {
    public:
        /** Constructor.
         *
         * @param executor Executor instance to run the timer on
         * @param command Command to poll for
         */
        async_wait_for_process_t(Executor & executor, std::string_view command)
            : state {std::make_shared<impl_t>(executor, command)}
        {
        }

        // No copying!
        async_wait_for_process_t(const async_wait_for_process_t &) = delete;
        async_wait_for_process_t & operator=(const async_wait_for_process_t &) = delete;

        /** Destructor.
         *
         * Cancels the polling, if still running.
         */
        ~async_wait_for_process_t() { cancel(); }

        /** Start the asynchronous polling.
         *
         * @tparam CompletionToken CompletionToken type
         * @tparam Rep Duration Duration tick type
         * @tparam Period Duration tick period ratio
         * @param interval Minimum interval between polls, maybe longer due to io_context contention
         * @param token Completion token
         * @return Nothing or a continuation, depending on @a token
         */
        template<typename CompletionToken, typename Rep, typename Period>
        auto start(std::chrono::duration<Rep, Period> interval, CompletionToken && token)
        {
            using namespace async::continuations;
            using poly_return_type = polymorphic_continuation_t<boost::system::error_code, std::set<int>>;

            state->cancel = false;
            return async_initiate<continuation_of_t<boost::system::error_code, std::set<int>>>(
                [interval, self = state]() mutable {
                    return start_with(boost::system::error_code {}, std::set<int> {})
                         | loop(
                               [=](auto ec, auto pids) {
                                   // If we have been cancelled without the timer being started, then set the ec
                                   // appropriately
                                   if (!ec && self->cancel) {
                                       ec = boost::asio::error::operation_aborted;
                                   }

                                   // Exit if there is an error from the timer, we've found some PIDs, or the process
                                   // has been cancelled
                                   const auto found_pids = !pids.empty();
                                   return start_with(!ec && !found_pids, ec, std::move(pids));
                               },
                               [=](auto /*ec*/, auto /*pids*/) mutable -> poly_return_type {
                                   auto pids = std::make_shared<std::set<int>>();
                                   auto poller = std::make_shared<async_proc_poller_t<strand_type>>(self->strand);

                                   return poller->async_poll(use_continuation,
                                                             [pids, self](int pid, const lib::FsEntry & path) mutable {
                                                                 if (check_path(self->command, self->real_path, path)) {
                                                                     pids->insert(pid);
                                                                 }
                                                             })
                                        | then([self, pids, interval](auto ec) -> poly_return_type {
                                              return start_on(self->strand)
                                                   | do_if_else([ec]() { return !!ec; },
                                                                [ec]() -> poly_return_type {
                                                                    return start_with(ec, std::set<int> {});
                                                                }, // Exit early if async_poll returned an error
                                                                [self, pids, interval]() mutable -> poly_return_type {
                                                                    return start_with()
                                                                         | do_if_else(
                                                                               [pids]() { return !pids->empty(); },
                                                                               [pids]() mutable {
                                                                                   // We've found some pids! Return the result
                                                                                   return start_with(
                                                                                       boost::system::error_code {},
                                                                                       std::move(*pids));
                                                                               },
                                                                               [self, interval]() mutable {
                                                                                   // Otherwise, queue up the next read
                                                                                   self->timer.expires_after(interval);
                                                                                   return self->timer.async_wait(
                                                                                              use_continuation)
                                                                                        | then([self](auto ec) {
                                                                                              return start_with(
                                                                                                  ec,
                                                                                                  std::set<int> {});
                                                                                          });
                                                                               });
                                                                });
                                          });
                               });
                },
                token);
        }

        /** Cancels the polling.
         *
         * This will return an operation_aborted ec in any waiting async handler.
         */
        void cancel()
        {
            // We use a flag AND cancel the timer, in case the user has decided to cancel before the timer has started
            state->cancel = true;
            boost::asio::dispatch(state->strand, [=]() {
                try {
                    state->timer.cancel();
                }
                catch (boost::system::system_error & e) {
                    LOG_WARNING("Timer cancellation failure in async_wait_for_process_t: %s", e.what());
                }
            });
        }

    private:
        using strand_type = boost::asio::strand<boost::asio::associated_executor_t<Executor>>;

        // We use a PIMPL-like idiom so that the polling loop is cancelled upon parent instance destruction, using
        // std::enable_shared_from_this could result in the loop being unstoppable if the 'handle' from the caller is
        // lost
        struct impl_t {
            impl_t(Executor & executor, std::string_view command)
                : strand {boost::asio::make_strand(executor)},
                  timer {strand},
                  command {command},
                  real_path(lib::FsEntry::create(std::string {command}).realpath()),
                  cancel {false}
            {
            }

            strand_type strand;
            boost::asio::steady_timer timer;
            std::string_view command;
            std::optional<lib::FsEntry> real_path;
            std::atomic_bool cancel;
        };

        [[nodiscard]] static bool check_path(std::string_view command,
                                             const std::optional<lib::FsEntry> & real_path,
                                             const lib::FsEntry & path)
        {
            if (!command.empty()) {
                const auto cmdline_file = lib::FsEntry::create(path, "cmdline");
                const auto cmdline = lib::readFileContents(cmdline_file);

                // cmdline is separated by nulls so use c_str() to extract the command name
                const std::string tested_command {cmdline.c_str()}; // NOLINT(readability-redundant-string-cstr)
                if (!tested_command.empty()) {
                    LOG_DEBUG("Wait for Process: Scanning '%s': cmdline[0] = '%s'",
                              path.path().c_str(),
                              tested_command.c_str());

                    // track it if they are the same string
                    if (command == tested_command) {
                        LOG_DEBUG("    Selected as cmdline matches");
                        return true;
                    }

                    // track it if they are the same file, or if they are the same basename
                    const auto tested_command_path = lib::FsEntry::create(tested_command);
                    const auto tested_real_path = tested_command_path.realpath();

                    // they are the same executable command
                    if (real_path && tested_real_path && (*real_path == *tested_real_path)) {
                        LOG_DEBUG("    Selected as realpath matches (%s)", real_path->path().c_str());
                        return true;
                    }

                    // the basename of the command matches the command name
                    // (e.g. /usr/bin/ls == ls)
                    if (tested_command_path.name() == command) {
                        LOG_DEBUG("    Selected as name matches");
                        return true;
                    }
                }
            }

            // check exe
            if (real_path) {
                const auto exe = lib::FsEntry::create(path, "exe");
                const auto tested_real_path = exe.realpath();

                // they are the same executable command
                if (tested_real_path && (*real_path == *tested_real_path)) {
                    LOG_DEBUG("Wait for Process: Selected as exe matches (%s)", real_path->path().c_str());
                    return true;
                }
            }

            return false;
        }

        std::shared_ptr<impl_t> state;
    };
}
