/* Copyright (C) 2021-2025 by Arm Limited. All rights reserved. */

#pragma once

#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"

#include <cstring>
#include <memory>
#include <utility>

#include <endian.h>

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

    /**
     * Wrapper / adapter for sending IPC messages containing timeline data.
     * Similar to ipc_annotations_sink_adapter_t. Expected to be one of these
     * objects per connection.
     */
    class ipc_timeline_sink_adapter_t {
    public:
        ipc_timeline_sink_adapter_t(std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink, ipc::annotation_uid_t id)
            : sink(std::move(sink)), id(id)
        {
        }

        /** "Handshake tag" (ESTATE header) to send before provided timeline data */
        static const std::vector<std::uint8_t> timeline_protocol_handshake_tag;

        /**
         * Send the 'new connection' IPC message.
         *
         * This does the following:
         * - The shell side of Gator is notified
         * - token is executed. The caller should arrange for this to call
         *   async_send_estate_header() if no error was indicated.
         */
        template<typename CompletionToken>
        void async_send_new_connection(CompletionToken && token)
        {
            sink->async_send_message(ipc::msg_annotation_new_conn_t {id}, std::forward<CompletionToken>(token));
        }

        /**
         * Send the ESTATE header. This should normally be called after
         * async_send_new_connection() has completed without an error being
         * passed to its token.
         *
         * This does the following:
         * - The ESTATE header (handshake tag) is sent
         * - token is executed
         */
        template<typename CompletionToken>
        void async_send_estate_header(CompletionToken && token)
        {
            sink->async_send_message(ipc::msg_gpu_timeline_handshake_tag_t {id, timeline_protocol_handshake_tag},
                                     std::forward<CompletionToken>(token));
        }

        /** Send the 'received bytes' IPC message */
        template<typename CompletionToken>
        void async_send_received_bytes(std::shared_ptr<std::vector<uint8_t>> & timeline_data, CompletionToken && token)
        {
            // Timeline data length in little-endian format (which should in practice be the CPU's ABI)
            uint32_t timeline_data_length_le = htole32(static_cast<uint32_t>(timeline_data->size()));

            // complete_message should contain, in the following order:
            //  - timeline data length (LE binary rather than human-readable)
            //  - timeline data
            std::vector<std::uint8_t> complete_message(sizeof(timeline_data_length_le) + timeline_data->size());
            size_t field_index = 0;

            memcpy(&complete_message[field_index], &timeline_data_length_le, sizeof(timeline_data_length_le));
            field_index += sizeof(timeline_data_length_le);

            memcpy(&complete_message[field_index], timeline_data->data(), timeline_data->size());

            sink->async_send_message(ipc::msg_gpu_timeline_recv_t {id, std::move(complete_message)},
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
