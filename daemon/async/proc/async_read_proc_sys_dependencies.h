/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

namespace async {
    /**
     * Reads the /proc sys dependencies synchronously, and sends the results via @a sender
     * asynchronously.
     *
     * @tparam Executor Executor type
     * @tparam Sender Sender type
     * @tparam CompletionToken CompletionToken type
     * @param executor Executor instance, typically the one used inside @a sender
     * @param sender Sends the data
     * @param token Called upon completion with an error_code
     * @return Nothing or a continuation, depending on @a CompletionToken
     */
    template<typename Executor, typename Sender, typename CompletionToken>
    auto async_read_proc_sys_dependencies(Executor & executor, Sender & sender, CompletionToken && token)
    {
        using namespace async::continuations;
        using poly_return_type = polymorphic_continuation_t<boost::system::error_code>;

        struct result_t {
            int pid;
            int tid;
            std::string comm;
            std::string exe;
        };

        return async_initiate<continuation_of_t<boost::system::error_code>>(
            [&]() mutable {
                auto results = std::make_shared<std::vector<result_t>>();
                auto poller = std::make_shared<async_proc_poller_t<Executor>>(executor);

                return poller->async_poll(
                           use_continuation,
                           [results](int pid,
                                     int tid,
                                     const lnx::ProcPidStatFileRecord & statRecord,
                                     const std::optional<lnx::ProcPidStatmFileRecord> & /*statmRecord*/,
                                     const std::optional<lib::FsEntry> & exe) {
                               results->push_back({pid, tid, statRecord.getComm(), exe ? exe->path() : ""});
                           })
                     | then([&sender, results](auto ec) mutable -> poly_return_type {
                           return start_with() | //
                                  do_if_else(
                                      [ec]() { return !!ec; },
                                      [ec]() { return start_with(ec); }, // Exit early if async_poll returned an error
                                      [&sender, results]() mutable {
                                          return start_with(results->begin(),
                                                            results->end(),
                                                            boost::system::error_code {})
                                               | loop([](auto it,
                                                         auto end,
                                                         auto ec) { return start_with(it != end, it, end, ec); },
                                                      [&sender, results](auto it, auto end, auto ec) mutable {
                                                          return sender.async_send_comms_frame(it->pid,
                                                                                               it->tid,
                                                                                               it->exe,
                                                                                               it->comm,
                                                                                               use_continuation)
                                                               | then([=](auto new_ec) mutable {
                                                                     // Don't stop on the first failure
                                                                     if (new_ec) {
                                                                         ec = new_ec;
                                                                     }

                                                                     return start_with(++it, end, ec);
                                                                 });
                                                      }) //
                                               | then([](auto /*it*/, auto /*end*/, auto ec) { return ec; });
                                      });
                       });
            },
            token);
    }
}
