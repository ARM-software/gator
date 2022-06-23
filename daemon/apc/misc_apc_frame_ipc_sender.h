/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Protocol.h"
#include "Time.h"
#include "agents/perf/perf_driver_summary.h"
#include "apc/perf_apc_frame_utils.h"
#include "apc/summary_apc_frame_utils.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "k/perf_event.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <boost/asio/async_result.hpp>

#include <sys/types.h>

namespace apc {

    class misc_apc_frame_ipc_sender_t {
    public:
        explicit misc_apc_frame_ipc_sender_t(std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink)
            : ipc_sink(std::move(ipc_sink)) {};

        template<typename CompletionToken>
        auto async_send_perf_events_attributes_frame(perf_event_attr const & pea, int key, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink,
                 bytes = apc::make_perf_events_attributes_frame(pea, key)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_keys_frame(lib::Span<uint64_t const> ids, lib::Span<int const> keys, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_keys_frame(ids, keys)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_old_keys_frame(lib::Span<int const> keys, lib::Span<const char> bytes, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_old_keys_frame(keys, bytes)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_format_frame(std::string_view format, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_format_frame(format)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_maps_frame(int pid, int tid, std::string_view maps, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_maps_frame(pid, tid, maps)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_comm_frame(int pid,
                                   int tid,
                                   std::string_view image,
                                   std::string_view comm,
                                   CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_comm_frame(pid, tid, image, comm)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_cpu_online_frame(monotonic_delta_t timestamp, int cpu, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_cpu_online_frame(timestamp, cpu)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_cpu_offine_frame(monotonic_delta_t timestamp, int cpu, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_cpu_offline_frame(timestamp, cpu)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_kallsyms_frame(std::string_view kallsyms, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_kallsyms_frame(kallsyms)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_perf_counters_frame(monotonic_delta_t timestamp,
                                            lib::Span<perf_counter_t const> counters,
                                            CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink,
                 bytes = apc::make_perf_counters_frame(timestamp, counters)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_header_page_frame(std::string_view header_page, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_header_page_frame(header_page)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_header_event_frame(std::string_view header_event, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_header_event_frame(header_event)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_summary_message(agents::perf::perf_driver_summary_state_t const & state,
                                        CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_summary_message(state)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

        template<typename CompletionToken>
        auto async_send_core_name(int core, int cpuid, std::string_view name, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_core_name_message(core, cpuid, name)](auto && handler) mutable {
                    ipc_sink->async_send_message(
                        ipc::msg_apc_frame_data_t {std::move(bytes)},
                        [h = std::forward<decltype(handler)>(handler)](auto const & ec,
                                                                       auto const & /* msg */) mutable { h(ec); });
                },
                token);
        }

    private:
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
    };

}
