/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "BufferUtils.h"
#include "ISender.h"
#include "agents/perf/async_buffer_builder.h"
#include "agents/perf/record_types.h"
#include "async/async_buffer.hpp"
#include "async/completion_handler.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/continuation_of.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/Span.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

#include <boost/asio/async_result.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {

    /**
     * Instances of frame_encoder_t are responsible for writing perf data records (events from the main
     * ring buffer, or data blocks from the aux ring buffer) into an asynchronous buffer. This hides
     * the complexity of:
     *  1. working out how much space will be needed;
     *  2. requesting that amount of space from the async buffer;
     *  3. waiting for that space to be available;
     *  4. writing the record into the space that was allocated.
     *
     * @tparam Executor The Boost Asio executor type that will be used to dispatch async requests.
     * @tparam RecordType The type that represents the perf data to be encoded.
     * @tparam Encoder A type that is capable of consuming an object of RecordType and writing it
     *                 into a buffer region.
     */
    template<typename Executor, typename RecordType, typename Encoder>
    class frame_encoder_t : public std::enable_shared_from_this<frame_encoder_t<Executor, RecordType, Encoder>> {
    private:
        // A type that keeps track of where we are reading from as the async operations progress.
        struct record_index_t {
            // which record in the span are we currently consuming?
            std::size_t record_number;
            // how far into that record did we get?
            std::size_t offset_in_record;

            // moves to the next record in the span
            void next()
            {
                record_number++;
                offset_in_record = 0;
            }
        };

        // A type that holds the state that's needed by each async step in the state machine.
        struct task_ctx_t {
            std::shared_ptr<frame_encoder_t> self;
            std::shared_ptr<async::async_buffer_t> send_buffer;
            int cpu;
            std::uint64_t tail_pointer;
            lib::Span<RecordType> records;
            record_index_t index;
        };

        Executor & executor;
        Encoder encoder;
        std::atomic<bool> task_running;

        void finish_current_task(task_ctx_t & task)
        {
            task.send_buffer.reset();
            task_running.store(false, std::memory_order_release);
        }

        auto co_handle_emit_record(task_ctx_t task,
                                   async::async_buffer_t::mutable_buffer_type buffer,
                                   async::async_buffer_t::commit_action_t action)
        {
            using namespace async::continuations;

            auto & record = task.records[task.index.record_number];

            auto new_offset = encoder.encode_into(buffer,
                                                  std::move(action),
                                                  record,
                                                  task.cpu,
                                                  task.tail_pointer,
                                                  task.index.offset_in_record);

            task.index.offset_in_record = new_offset;

            // loop back and read the next chunk
            return start_on<on_executor_mode_t::post>(executor) //
                 | then([st = this->shared_from_this(), task = std::move(task)]() mutable {
                       return st->co_request_space_for_index(std::move(task));
                   });
        }

        async::continuations::polymorphic_continuation_t<boost::system::error_code, std::size_t>
        co_request_space_for_index(task_ctx_t task)
        {
            using namespace async::continuations;

            // if we've reached the last record call the completion handler
            if (task.index.record_number >= task.records.size()) {
                // call this first in case the handler decides to throw
                finish_current_task(task);
                return start_with(boost::system::error_code {}, task.index.record_number);
            }

            auto & record = task.records[task.index.record_number];

            // if we've finished sending this record move to the next one
            if (task.index.offset_in_record >= record.number_of_elements()) {
                task.index.next();
                return co_request_space_for_index(std::move(task));
            }

            // how much buffer space do we need for this record?
            const int bytes_requested = encoder.get_bytes_required(record, task.index.offset_in_record);
            const auto amount_to_request =
                Encoder::max_header_size + std::min(bytes_requested, Encoder::max_payload_size);

            auto sb = task.send_buffer;

            return sb->async_request_space(amount_to_request, use_continuation) //
                 | then([st = this->shared_from_this(),
                         task = std::move(task)](bool success, auto buffer, auto commit_action) mutable
                        -> polymorphic_continuation_t<boost::system::error_code, std::size_t> {
                       if (!success) {
                           // clean this up first in case the handler decides to throw
                           st->finish_current_task(task);
                           return start_with(boost::asio::error::make_error_code(boost::asio::error::no_memory),
                                             task.index.record_number);
                       }
                       return st->co_handle_emit_record(std::move(task), std::move(buffer), std::move(commit_action));
                   });
        }

        template<typename CompletionToken>
        auto async_encode(task_ctx_t task, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate<continuation_of_t<boost::system::error_code, std::size_t>>(
                [st = this->shared_from_this(), task = std::move(task)]() mutable {
                    // don't allow 2 tasks to run concurrently. Even though they're dispatched on the same executor
                    // the individual steps in the state machine would end up interleaved and cause problems.
                    return start_with() //
                         | do_if_else([st]() { return st->task_running.exchange(true); },
                                      [st]() {
                                          return start_with(
                                              boost::asio::error::make_error_code(boost::asio::error::already_started),
                                              0);
                                      },
                                      [st, task = std::move(task)]() mutable {
                                          return start_on(st->executor) //
                                               | then([st, task = std::move(task)]() mutable {
                                                     return st->co_request_space_for_index(std::move(task));
                                                 });
                                      });
                },
                std::forward<CompletionToken>(token));
        }

    public:
        explicit frame_encoder_t(Executor & executor) : executor(executor), task_running(false) {}

        /**
         * Asynchronously encode an array of perf data into an asynchronous buffer.
         *
         * @param send_buffer The async buffer from which space will be allocated.
         * @param cpu The index of the CPU from which the data were sampled.
         * @param tail_pointer A snapshot of the ring buffer's tail pointer at the time the records
         *  were taken.
         * @param records A span of objects representing the data to be consumed from the perf
         *  ring buffer.
         */
        template<typename CompletionToken>
        auto async_encode(std::shared_ptr<async::async_buffer_t> send_buffer,
                          int cpu,
                          std::uint64_t tail_pointer,
                          lib::Span<RecordType> records,
                          CompletionToken && token)
        {
            return async_encode(
                task_ctx_t {
                    this->shared_from_this(),
                    std::move(send_buffer),
                    cpu,
                    tail_pointer,
                    std::move(records),
                    {0, 0},
                },
                std::forward<CompletionToken>(token));
        }
    };

}
