/* Copyright (C) 2021-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace agents {
    /**
     * Simple wrapper / adapter for sending IPC messages from the common socket worker functions
     */
    class ipc_annotations_sink_adapter_t {
    public:
        static std::vector<std::uint8_t> reclaim_buffer(ipc::msg_annotation_recv_bytes_t && msg)
        {
            return std::move(msg.suffix);
        }

        ipc_annotations_sink_adapter_t(std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink, ipc::annotation_uid_t id)
            : sink(std::move(sink)), id(id)
        {
        }

        /** Send the 'new connection' IPC message */
        template<typename CompletionToken>
        void async_send_new_connection(CompletionToken && token)
        {
            sink->async_send_message(ipc::msg_annotation_new_conn_t {id}, std::forward<CompletionToken>(token));
        }

        /** Send the 'received bytes' IPC message */
        template<typename CompletionToken>
        void async_send_received_bytes(std::vector<std::uint8_t> && bytes, CompletionToken && token)
        {
            sink->async_send_message(ipc::msg_annotation_recv_bytes_t {id, std::move(bytes)},
                                     std::forward<CompletionToken>(token));
        }

        /** Send the 'close connection' IPC message */
        template<typename CompletionToken>
        void async_send_close_connection(CompletionToken && token)
        {
            sink->async_send_message(
                ipc::msg_annotation_close_conn_t {
                    id,
                },
                std::forward<CompletionToken>(token));
        }

    private:
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink;
        ipc::annotation_uid_t id;
    };
}
