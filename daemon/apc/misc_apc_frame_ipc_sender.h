/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "ISender.h"
#include "Protocol.h"
#include "Time.h"
#include "agents/perf/events/types.hpp"
#include "agents/perf/perf_driver_summary.h"
#include "apc/perf_apc_frame_utils.h"
#include "apc/summary_apc_frame_utils.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "k/perf_event.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/async_result.hpp>

#include <sys/types.h>

namespace apc {

    namespace detail {
        inline std::string_view trim_to_max(std::string_view str)
        {
            constexpr std::size_t space_for_header = 64U;
            str = str.substr(0, ISender::MAX_RESPONSE_LENGTH - space_for_header);
            str = str.substr(0, str.rfind('\n'));
            return str;
        }
    }

    class misc_apc_frame_ipc_sender_t {
    public:
        explicit misc_apc_frame_ipc_sender_t(std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink)
            : ipc_sink(std::move(ipc_sink)) {};

        template<typename CompletionToken>
        auto async_send_perf_events_attributes_frame(perf_event_attr const & pea, int key, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_perf_events_attributes_frame(pea, key)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_keys_frame(
            lib::Span<std::pair<agents::perf::perf_event_id_t, agents::perf::gator_key_t> const> mappings,
            CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_keys_frame(mappings)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_format_frame(std::string_view format, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_format_frame(format)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_maps_frame(int pid, int tid, std::string_view maps, CompletionToken && token)
        {
            using namespace async::continuations;

            runtime_assert(maps.size() <= ISender::MAX_RESPONSE_LENGTH, "too large maps file received");

            // limit size
            if (maps.size() >= ISender::MAX_RESPONSE_LENGTH) {
                maps = detail::trim_to_max(maps);
            }

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_maps_frame(pid, tid, maps)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_comm_frame(int pid,
                                   int tid,
                                   std::string_view image,
                                   std::string_view comm,
                                   CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_comm_frame(pid, tid, image, comm)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_cpu_online_frame(monotonic_delta_t timestamp, int cpu, bool online, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink,
                 bytes = (online ? apc::make_cpu_online_frame(timestamp, cpu) //
                                 : apc::make_cpu_offline_frame(timestamp, cpu))](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }
        template<typename CompletionToken>
        auto async_send_kallsyms_frame(std::string_view kallsyms, CompletionToken && token)
        {
            using namespace async::continuations;

            runtime_assert(kallsyms.size() <= ISender::MAX_RESPONSE_LENGTH, "too large kallsyms received");

            // limit size
            if (kallsyms.size() >= ISender::MAX_RESPONSE_LENGTH) {
                kallsyms = detail::trim_to_max(kallsyms);
            }

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_kallsyms_frame(kallsyms)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_perf_counters_frame(monotonic_delta_t timestamp,
                                            lib::Span<perf_counter_t const> counters,
                                            CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_perf_counters_frame(timestamp, counters)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_header_page_frame(std::string_view header_page, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_header_page_frame(header_page)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_header_event_frame(std::string_view header_event, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_header_event_frame(header_event)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_summary_message(agents::perf::perf_driver_summary_state_t const & state,
                                        CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_summary_message(state)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        template<typename CompletionToken>
        auto async_send_core_name(int core, int cpuid, std::string_view name, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code)>(
                [ipc_sink = ipc_sink, bytes = apc::make_core_name_message(core, cpuid, name)](auto && sc) mutable {
                    submit(ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(bytes)},
                                                        use_continuation) //
                               | then([](auto const & ec, auto const &) { return ec; }),
                           std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
    };

}
