/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "async/proc/async_proc_poller.h"

#include <boost/asio/error.hpp>
#include <boost/system/detail/error_code.hpp>

namespace async {

    /**
     * Reads the /proc maps and sends the results via @a sender asynchronously.
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
    auto async_read_proc_maps(Executor & executor, Sender & sender, CompletionToken && token)
    {
        using namespace async::continuations;
        using boost_error_type = boost::system::error_code;

        return async_initiate<continuation_of_t<boost::system::error_code>>(
            [&, ec_record = std::make_shared<boost_error_type>()]() mutable {
                auto poller = std::make_shared<async_proc_poller_t<Executor>>(executor);

                return poller->async_poll(use_continuation, [&sender, ec_record](int pid, const lib::FsEntry & entry) {
                    const lib::FsEntry mapsFile = lib::FsEntry::create(entry, "maps");
                    if (!mapsFile.exists()) {
                        *ec_record = boost::asio::error::not_found;
                        return;
                    }
                    else if (!mapsFile.canAccess(true, false, false)) {
                        *ec_record = boost::asio::error::no_permission;
                        return;
                    }
                    return sender.async_send_maps_frame(pid,
                                                        pid,
                                                        lib::readFileContents(mapsFile),
                                                        [ec_record](boost::system::error_code new_ec) mutable {
                                                            *ec_record = (!!new_ec) ? new_ec : *ec_record;
                                                        });
                }) | then([ec_record](auto ec) { return ec ? ec : *ec_record; });
            },
            token);
    }
}
