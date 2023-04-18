/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

#include <boost/system/error_code.hpp>

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
     * @param filter filter callable that decides whether or not to send a specific process's details
     * @return Nothing or a continuation, depending on @a CompletionToken
     */
    template<typename Executor, typename Sender, typename Filter, typename CompletionToken>
    auto async_read_proc_sys_dependencies(Executor && executor,
                                          std::shared_ptr<Sender> sender,
                                          Filter && filter,
                                          CompletionToken && token)
    {
        using namespace async::continuations;
        using poly_return_type = polymorphic_continuation_t<boost::system::error_code>;

        return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
            [poller = make_async_proc_poller(std::forward<Executor>(executor)),
             sender = std::move(sender),
             filter = std::forward<Filter>(filter)]() mutable {
                return poller->async_poll(use_continuation,
                                          [sender = std::move(sender), filter = std::move(filter)](
                                              int pid,
                                              int tid,
                                              const lnx::ProcPidStatFileRecord & statRecord,
                                              const std::optional<lnx::ProcPidStatmFileRecord> & /*statmRecord*/,
                                              const std::optional<std::string> & exe) -> poly_return_type {
                                              // filter the pid/tid
                                              if (!filter(pid, tid)) {
                                                  return start_with(boost::system::error_code {});
                                              }
                                              return sender->async_send_comm_frame(pid,
                                                                                   tid,
                                                                                   exe ? *exe : "",
                                                                                   statRecord.getComm(),
                                                                                   use_continuation);
                                          });
            },
            token);
    }

    template<typename Executor, typename Sender, typename CompletionToken>
    auto async_read_proc_sys_dependencies(Executor & executor, std::shared_ptr<Sender> sender, CompletionToken && token)
    {
        return async_read_proc_sys_dependencies(
            executor,
            std::move(sender),
            [](int, int) { return true; },
            std::forward<CompletionToken>(token));
    }
}
