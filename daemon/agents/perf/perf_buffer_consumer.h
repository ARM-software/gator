/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/perf/events/perf_ringbuffer_mmap.hpp"
#include "agents/perf/record_types.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/continuation_of.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "ipc/raw_ipc_channel_sink.h"

#include <atomic>
#include <map>
#include <memory>
#include <set>

#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {
    /**
     * This class consumes the contents of the perf mmap ringbuffers, outputing perf data apc frames and perf aux apc frames.
     * It is not responsible for monitoring of the perf file descriptors / periodic timer (these are handled elsewhere), but it provides
     * an interface where some other caller can trigger the data in the ringbuffer(s) to be consumed.
     */
    class perf_buffer_consumer_t : public std::enable_shared_from_this<perf_buffer_consumer_t> {
    public:
        perf_buffer_consumer_t(boost::asio::io_context & context,
                               std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink,
                               std::size_t one_shot_mode_limit)
            : one_shot_mode_limit(one_shot_mode_limit), ipc_sink(std::move(ipc_sink)), strand(context)
        {
        }

        /**
         * Insert a mmap into the consumer
         *
         * @param cpu The cpu the mmap is associated with
         * @param mmap The mmap object
         */
        template<typename CompletionToken>
        auto async_add_ringbuffer(int cpu, std::shared_ptr<perf_ringbuffer_mmap_t> mmap, CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_DEBUG("Add new mmap request for %d", cpu);

            return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
                [st = shared_from_this(), cpu, mmap = std::move(mmap)]() mutable {
                    return start_on(st->strand) //
                         | then([st, cpu, mmap = std::move(mmap)]() mutable {
                               LOG_DEBUG("Added new mmap for %d", cpu);

                               // validate the pointer
                               if ((!mmap) || (!mmap->has_data())) {
                                   return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
                               }

                               // insert it into the map
                               auto [it, inserted] = st->per_cpu_mmaps.try_emplace(cpu, std::move(mmap));
                               (void) it;

                               if (!inserted) {
                                   LOG_DEBUG("... failed, as already has mmap");
                                   return boost::system::errc::make_error_code(
                                       boost::system::errc::device_or_resource_busy);
                               }

                               // success
                               return boost::system::error_code {};
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Cause the mmap associated with `cpu` to be polled and any data to be written out to the capture.
         *
         * The operation will complete successfully if the cpu is already in the process of being polled by some other trigger, or if the cpu currently doesn't have any mmap associated with it.
         *
         * @param cpu The cpu for which the associated mmap should be polled
         */
        template<typename CompletionToken>
        auto async_poll(int cpu, CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("Poll requested for %d", cpu);

            return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
                [st = shared_from_this(), cpu]() mutable {
                    return start_on(st->strand) //
                         | then([cpu, st]() mutable -> polymorphic_continuation_t<boost::system::error_code> {
                               LOG_TRACE("Poll started for %d", cpu);

                               auto mmap_it = st->per_cpu_mmaps.find(cpu);
                               // ignore cpus that don't exist; its probably just poll_all
                               if (mmap_it == st->per_cpu_mmaps.end()) {
                                   LOG_TRACE("No such mmap found for %d", cpu);
                                   return start_with(boost::system::error_code {});
                               }

                               // if it is already being polled, also ignore the request
                               if (!st->busy_cpus.insert(cpu).second) {
                                   LOG_TRACE("Already polling %d", cpu);
                                   return start_with(boost::system::error_code {});
                               }

                               // ok, poll it
                               return do_poll(st, mmap_it->second, cpu);
                           });
                },
                token);
        }

        /**
         * Cause the mmap for all currently tracked cpus to be polled.
         */
        template<typename CompletionToken>
        auto async_poll_all(CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("Poll all requested");

            return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
                [st = shared_from_this()]() mutable {
                    return start_on(st->strand) //
                         | then([st]() mutable {
                               return start_with<std::size_t, std::size_t, boost::system::error_code>(
                                          0,
                                          st->per_cpu_mmaps.size(),
                                          {}) //
                                    | loop(
                                          [](std::size_t n, std::size_t count, boost::system::error_code ec) { //
                                              return start_with((n < count) && !ec, n, count, ec);
                                          },
                                          [st](std::size_t n, std::size_t count, boost::system::error_code /*ec*/) {
                                              return st->async_poll(n, use_continuation) //
                                                   | then([n, count](auto ec) { return start_with(n + 1, count, ec); });
                                          })
                                    | then([](std::size_t /*n*/, std::size_t /*count*/, boost::system::error_code ec) {
                                          LOG_TRACE("Poll all completed (ec=%s)", ec.message().c_str());
                                          return ec;
                                      });
                           });
                },
                token);
        }

        /**
         * Remove the mmap associated with some cpu.
         *
         * The mmap will be polled one more time before removal, and any currently active poll operations will complete successfully in parallel.
         *
         * @param cpu The cpu for which the associated mmap should be removed
         */
        template<typename CompletionToken>
        auto async_remove_ringbuffer(int cpu, CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("Remove mmap requested for %d", cpu);

            return async_initiate_cont<continuation_of_t<boost::system::error_code>>(
                [st = shared_from_this(), cpu]() mutable {
                    return start_on(st->strand) //
                         | then([st, cpu]() {
                               LOG_TRACE("Remove mmap marked for %d", cpu);
                               st->removed_cpus.insert(cpu);
                           }) //
                         | st->async_poll(cpu, use_continuation);
                },
                token);
        }

        /**
         * Wait for notification that the required number of bytes is sent in one-shot mode
         * NB: will never notify if one-shot mode is disabled
         */
        template<typename CompletionToken>
        auto async_wait_one_shot_full(CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("Wait oneshot-full requested");

            return async_initiate_explicit<void()>(
                [st = shared_from_this()](auto && sc) mutable {
                    submit(start_on(st->strand) //
                               | then([st, sc = sc.move()]() mutable {
                                     LOG_TRACE("Wait oneshot-full started");

                                     // notify directly if already full
                                     if (st->is_one_shot_full()) {
                                         resume_continuation(st->strand.context(), std::move(sc));
                                     }

                                     // save it for later
                                     runtime_assert(!st->one_shot_mode_observer,
                                                    "Cannot register two one-shot mode observers");

                                     st->one_shot_mode_observer = std::move(sc);
                                 }),
                           sc.get_exceptionally());
                },
                token);
        }

        /** Is the output data full wrt one-shot mode */
        [[nodiscard]] bool is_one_shot_full() const
        {
            bool result = (one_shot_mode_limit > 0)
                       && (cumulative_bytes_sent_apc_frames.load(std::memory_order_acquire) >= one_shot_mode_limit);

            if (result) {
                LOG_DEBUG("Cumulative bytes sent:%zu, One shot mode limit:%zu",
                          cumulative_bytes_sent_apc_frames.load(std::memory_order_acquire),
                          one_shot_mode_limit);
            }
            return result;
        }

        /** Manually trigger the one-shot-mode callback */
        void trigger_one_shot_mode()
        {
            // set  both to non-zero to mark as triggered
            one_shot_mode_limit = 1;
            cumulative_bytes_sent_apc_frames.store(1, std::memory_order_release);

            // trigger if possible
            boost::asio::post(strand, [st = this->shared_from_this()]() {
                async::continuations::stored_continuation_t<> one_shot_mode_observer {
                    std::move(st->one_shot_mode_observer)};

                if (one_shot_mode_observer) {
                    resume_continuation(st->strand.context(), std::move(one_shot_mode_observer));
                }
            });
        }

    private:
        /**
         * Send one apc_frame IPC message, returns the head, new-tail and error code as required at the end of each send loop iteration
         *
         * @param st The this pointer for the perf_buffer_consumer_t that made the request
         * @param cpu The cpu associated with the request
         * @param buffer The apc_frame data buffer
         * @param head The aux_head or data_head value
         * @param tail The new value for aux_tail or data_tail after the send completes
         * @return A continuation producing the head, new-tail and error code values
         */
        static async::continuations::polymorphic_continuation_t<std::uint64_t, std::uint64_t, boost::system::error_code>
        do_send_msg(std::shared_ptr<perf_buffer_consumer_t> const & st,
                    int cpu,
                    std::vector<uint8_t> buffer,
                    std::uint64_t head,
                    std::uint64_t tail);

        /**
         * Common to both aux and data send loops, this function will extract the head and tail field, then iterate over the buffer until tail == head, sending some chunk and then moving tail
         *
         * @tparam HeadField A pointer-to-member-variable for the aux_head or data_head field in the mmap header
         * @tparam TailField A pointer-to-member-variable for the aux_tail or data_tail field in the mmap header
         * @tparam Op The loop body operation, that encodes and sends some chunk of the mmap. Must return a continuation over `(head, new-tail, error-code)`
         * @param mmap The mmap object
         * @param cpu The cpu associated with this mmap
         * @param op The operation
         * @return a continuation that produces an error code
         */
        template<__u64 perf_event_mmap_page::*HeadField, __u64 perf_event_mmap_page::*TailField, typename Op>
        static async::continuations::polymorphic_continuation_t<boost::system::error_code, bool> do_send_common(
            std::shared_ptr<perf_buffer_consumer_t> const & st,
            std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
            int cpu,
            Op && op);

        /**
         * Read and send the aux section
         */
        static async::continuations::polymorphic_continuation_t<boost::system::error_code, bool> do_send_aux_section(
            std::shared_ptr<perf_buffer_consumer_t> const & st,
            std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
            int cpu,
            boost::system::error_code ec_from_data,
            bool modified_from_data);

        /**
         * Read and send the data section
         */
        static async::continuations::polymorphic_continuation_t<boost::system::error_code, bool> do_send_data_section(
            std::shared_ptr<perf_buffer_consumer_t> const & st,
            std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
            int cpu);

        /**
         * Construct the poll operation for one cpu
         *
         * @param st The shared this
         * @param mmap The mmap being read from
         * @param cpu The cpu to poll
         */
        [[nodiscard]] static async::continuations::polymorphic_continuation_t<boost::system::error_code> do_poll(
            std::shared_ptr<perf_buffer_consumer_t> const & st,
            std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
            int cpu);

        std::atomic_size_t cumulative_bytes_sent_apc_frames {0};
        std::size_t one_shot_mode_limit {0};
        std::set<int> busy_cpus {};
        std::set<int> removed_cpus {};
        std::map<int, std::shared_ptr<perf_ringbuffer_mmap_t>> per_cpu_mmaps {};
        std::shared_ptr<ipc::raw_ipc_channel_sink_t> ipc_sink;
        async::continuations::stored_continuation_t<> one_shot_mode_observer {};
        boost::asio::io_context::strand strand;
    };
}
