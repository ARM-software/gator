/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

#include <set>

#include <boost/asio/error.hpp>
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
        async_wait_for_process_t(Executor const & executor, std::string_view command)
            : state {std::make_shared<impl_t>(executor, command)}
        {
        }

        // No copying!
        async_wait_for_process_t(const async_wait_for_process_t &) = delete;
        async_wait_for_process_t & operator=(const async_wait_for_process_t &) = delete;

        async_wait_for_process_t(async_wait_for_process_t &&) noexcept = default;
        async_wait_for_process_t & operator=(async_wait_for_process_t &&) noexcept = default;

        /** Destructor.
         *
         * Cancels the polling, if still running.
         */
        ~async_wait_for_process_t()
        {
            // Check state so we don't try to cancel on a moved-from instance
            if (state) {
                cancel();
            }
        }

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

            state->cancel = false;
            return async_initiate_cont<continuation_of_t<boost::system::error_code, std::set<int>>>(
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
                               [=](auto const & /*ec*/, auto const & /*pids*/) mutable -> poly_return_type {
                                   // poll proc for all pids/tids that match our requirements
                                   return poll_once(interval, self);
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
            boost::asio::post(state->strand, [self = state]() {
                try {
                    LOG_DEBUG("Cancelling wait-process polling");
                    self->cancel = true;
                    self->timer.cancel();
                }
                catch (boost::system::system_error & e) {
                    LOG_WARNING("Timer cancellation failure in async_wait_for_process_t: %s", e.what());
                }
            });
        }

    private:
        using strand_type = decltype(boost::asio::make_strand(std::declval<Executor>()));
        using poly_return_type =
            async::continuations::polymorphic_continuation_t<boost::system::error_code, std::set<int>>;
        using poly_error_type = async::continuations::polymorphic_continuation_t<boost::system::error_code>;

        // We use a PIMPL-like idiom so that the polling loop is cancelled upon parent instance destruction, using
        // std::enable_shared_from_this could result in the loop being unstoppable if the 'handle' from the caller is
        // lost
        struct impl_t {
            impl_t(Executor const & executor, std::string_view command)
                : executor(executor),
                  strand {boost::asio::make_strand(executor)},
                  timer {strand},
                  command {command},
                  real_path(lib::FsEntry::create(std::string {command}).realpath()),
                  cancel {false}
            {
            }

            Executor executor;
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
                    LOG_TRACE("Wait for Process: Scanning '%s': cmdline[0] = '%s'",
                              path.path().c_str(),
                              tested_command.c_str());

                    // track it if they are the same string
                    if (command == tested_command) {
                        LOG_TRACE("    Selected as cmdline matches");
                        return true;
                    }

                    // track it if they are the same file, or if they are the same basename
                    const auto tested_command_path = lib::FsEntry::create(tested_command);
                    const auto tested_real_path = tested_command_path.realpath();

                    // they are the same executable command
                    if (real_path && tested_real_path && (*real_path == *tested_real_path)) {
                        LOG_TRACE("    Selected as realpath matches (%s)", real_path->path().c_str());
                        return true;
                    }

                    // the basename of the command matches the command name
                    // (e.g. /usr/bin/ls == ls)
                    if (tested_command_path.name() == command) {
                        LOG_TRACE("    Selected as name matches");
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
                    LOG_TRACE("Wait for Process: Selected as exe matches (%s)", real_path->path().c_str());
                    return true;
                }
            }

            return false;
        }

        template<typename Rep, typename Period>
        static poly_return_type poll_once(std::chrono::duration<Rep, Period> interval, std::shared_ptr<impl_t> self)
        {
            using namespace async::continuations;

            auto pids = std::make_shared<std::set<int>>();
            auto poller = make_async_proc_poller(self->executor);

            return poller->async_poll(use_continuation,
                                      [pids, self](int pid, const lib::FsEntry & path) mutable -> poly_error_type {
                                          if (self->cancel) {
                                              return start_with(
                                                  boost::system::error_code {boost::asio::error::operation_aborted});
                                          }
                                          if (check_path(self->command, self->real_path, path)) {
                                              pids->insert(pid);
                                          }
                                          return start_with(boost::system::error_code {});
                                      })
                 | then([self, pids, interval](auto ec) -> poly_return_type {
                       // Exit early if async_poll returned an error
                       if (ec) {
                           return start_with(ec, std::set<int> {});
                       }

                       // We've found some pids? Return the result
                       if (!pids->empty()) {
                           return start_with(boost::system::error_code {}, std::move(*pids));
                       }

                       // Otherwise, queue up the next read
                       return start_on(self->strand) //
                            | then([self, interval]() {
                                  self->timer.expires_after(interval);
                                  return self->timer.async_wait(use_continuation)
                                       | then([self](auto ec) { return start_with(ec, std::set<int> {}); });
                              });
                   });
        }

        std::shared_ptr<impl_t> state;
    };

    template<typename Executor, std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    auto make_async_wait_for_process(Executor const & ex, std::string_view command)
    {
        return std::make_shared<async_wait_for_process_t<Executor>>(ex, command);
    }

    template<typename ExecutionContext, std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    auto make_async_wait_for_process(ExecutionContext & context, std::string_view command)
    {
        return make_async_wait_for_process(context.get_executor(), command);
    }
}
