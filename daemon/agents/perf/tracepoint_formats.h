/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "GatorException.h"
#include "Logging.h"
#include "agents/perf/async_buffer_builder.h"
#include "apc/misc_apc_frame_ipc_sender.h"
#include "async/async_buffer.hpp"
#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Span.h"

#include <string_view>
#include <vector>

namespace agents::perf {

    class tracepoint_formats_t : public std::enable_shared_from_this<tracepoint_formats_t> {

    public:
        tracepoint_formats_t(const TraceFsConstants & traceFsConstants,
                             std::shared_ptr<apc::misc_apc_frame_ipc_sender_t> sender)
            : traceFsConstants(traceFsConstants), sender(sender) {};

        template<typename CompletionToken>
        auto async_send_tracepoint_formats(lib::Span<std::string_view> tracepoint_names, CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [st = shared_from_this(), tracepoint_names_local = tracepoint_names]() mutable {
                    return start_with(tracepoint_names_local.begin(), boost::system::error_code {})
                         | loop(
                               [st, tracepoint_names_local](auto it, auto ec) {
                                   return start_with(
                                       (it != tracepoint_names_local.end() && ec == boost::system::error_code {}),
                                       it,
                                       ec);
                               },
                               [st](auto it, auto ec) mutable {
                                   return st->continue_send_tracepoint_formats(*it)
                                        | then([=](auto ec) mutable { return start_with(++it, ec); });
                               })
                         | then([](auto /*it*/, auto ec) { return ec; });
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_tracepoint_header_page(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [st = shared_from_this()](auto && handler) {
                    st->do_send_tracepoint_header_page_or_event_frame(std::move(handler), HEADER_PAGE);
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_tracepoint_header_event(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [st = shared_from_this()](auto && handler) {
                    st->do_send_tracepoint_header_page_or_event_frame(std::move(handler), HEADER_EVENT);
                },
                token);
        }

    private:
        static constexpr std::string_view HEADER_PAGE = "header_page";
        static constexpr std::string_view HEADER_EVENT = "header_event";
        static constexpr std::string_view FORMAT = "format";
        static constexpr std::string_view PATH_SEPARATOR = "/";

        const TraceFsConstants & traceFsConstants;
        std::shared_ptr<apc::misc_apc_frame_ipc_sender_t> sender;

        async::continuations::polymorphic_continuation_t<boost::system::error_code> continue_send_tracepoint_formats(
            std::string_view tracepoint_name)
        {
            using namespace async::continuations;

            auto path = lib::FsEntry::create(std::string(traceFsConstants.path__events) + PATH_SEPARATOR.data()
                                             + tracepoint_name.data() + PATH_SEPARATOR.data() + FORMAT.data()); //

            if (!path.canAccess(true, false, false)) {
                LOG_DEBUG("Can't access file %s", path.path().c_str());
                return start_with(boost::asio::error::make_error_code(boost::asio::error::no_permission));
            }

            auto format_contents = path.readFileContents();
            if (format_contents.empty()) {
                LOG_DEBUG("File (%s) content is empty", path.path().c_str());
                return start_with(boost::asio::error::make_error_code(boost::asio::error::misc_errors::not_found));
            }
            return sender->async_send_format_frame(format_contents, use_continuation);
        }

        template<typename Handler>
        void do_send_tracepoint_header_page_or_event_frame(Handler && handler, std::string_view fs_detail)
        {
            auto path = lib::FsEntry::create(lib::Format() << traceFsConstants.path__events << PATH_SEPARATOR.data()
                                                           << fs_detail.data()); //
            if (!path.canAccess(true, false, false)) {
                LOG_DEBUG("Can't access file %s", path.path().c_str());
                handler(boost::asio::error::make_error_code(boost::asio::error::no_permission));
                return;
            }
            auto format_contents = path.readFileContents();
            if (format_contents.empty()) {
                LOG_DEBUG("File (%s) content is empty", path.path().c_str());
                handler(boost::asio::error::make_error_code(boost::asio::error::misc_errors::not_found));
                return;
            }
            if (fs_detail == HEADER_EVENT) {
                sender->async_send_header_event_frame(format_contents, std::move(handler));
            }
            else if (fs_detail == HEADER_PAGE) {
                sender->async_send_header_page_frame(format_contents, std::move(handler));
            }
        }
    };
}
