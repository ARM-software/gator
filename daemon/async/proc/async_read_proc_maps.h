/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

#include <memory>

#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

namespace async {

    /**
     * Reads the /proc maps and sends the results via @a sender asynchronously.
     *
     * @tparam Executor Executor type
     * @tparam Sender Sender type
     * @tparam CompletionToken CompletionToken type
     * @param executor Executor instance, typically the one used inside @a sender
     * @param sender Sends the data
     * @param filter filter callable that decides whether or not to send a specific process's details
     * @param token Called upon completion with an error_code
     * @return Nothing or a continuation, depending on @a CompletionToken
     */
    template<typename Executor, typename Sender, typename Filter, typename CompletionToken>
    auto async_read_proc_maps(Executor && executor,
                              std::shared_ptr<Sender> sender,
                              Filter && filter,
                              CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
            [poller = make_async_proc_poller(std::forward<Executor>(executor)),
             sender = std::move(sender),
             filter = std::forward<Filter>(filter)]() mutable {
                return poller->async_poll(use_continuation,
                                          [sender, filter = std::move(filter)](int pid, const lib::FsEntry & entry)
                                              -> polymorphic_continuation_t<boost::system::error_code> {
                                              // check filter
                                              if (!filter(pid)) {
                                                  return start_with(boost::system::error_code {});
                                              }

                                              // missing or inaccessible file is not an error
                                              const lib::FsEntry mapsFile = lib::FsEntry::create(entry, "maps");

                                              if (!mapsFile.exists()) {
                                                  return start_with(boost::system::error_code {});
                                              }

                                              if (!mapsFile.canAccess(true, false, false)) {
                                                  return start_with(boost::system::error_code {});
                                              }

                                              // send the contents
                                              return sender->async_send_maps_frame(pid,
                                                                                   pid,
                                                                                   lib::readFileContents(mapsFile),
                                                                                   use_continuation);
                                          });
            },
            token);
    }

    template<typename Executor, typename Sender, typename CompletionToken>
    auto async_read_proc_maps(Executor & executor, std::shared_ptr<Sender> sender, CompletionToken && token)
    {
        return async_read_proc_maps(
            executor,
            std::move(sender),
            [](int) { return true; },
            std::forward<CompletionToken>(token));
    }
}
