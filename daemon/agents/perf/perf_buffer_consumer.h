/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/apc_encoders.h"
#include "agents/perf/detail/perf_buffer_consumer_detail.h"
#include "agents/perf/frame_encoder.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/use_continuation.h"
#include "lib/Span.h"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {

    /**
     * Instances of this class track and manage the perf buffers across multiple CPUs. They are responsible
     * for initiating a chain of async operations that read from each buffer and feed the results into an
     * intermediate buffer.
     */
    class perf_buffer_consumer_t : public std::enable_shared_from_this<perf_buffer_consumer_t> {
        using strand_type = boost::asio::io_context::strand;

        template<typename CompletionToken>
        auto async_send(std::shared_ptr<detail::perf_consume_op_t<strand_type>> op,
                        std::shared_ptr<async::async_buffer_t> intermediate_buffer,
                        CompletionToken && token)
        {
            return op->async_send(detail::data_encode_op_t<data_encoder_type> {data_encoder, intermediate_buffer},
                                  detail::aux_encode_op_t<aux_encoder_type> {aux_encoder, intermediate_buffer},
                                  std::forward<CompletionToken>(token));
        }

    public:
        /**
         * Constructor.
         *
         * @param io I/O context to use
         * @param config Buffer configuration
         * @exception GatorException Thrown if @a config fails validation
         */
        perf_buffer_consumer_t(boost::asio::io_context & io, buffer_config_t config);

        [[nodiscard]] std::size_t get_data_buffer_length() const { return config.data_buffer_size; }

        [[nodiscard]] std::size_t get_aux_buffer_length() const { return config.aux_buffer_size; }

        /**
         * Start tracking a perf event file descriptor for a specific CPU.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param fd The perf event fd.
         * @param cpu The CPU number that the fd is linked to.
         * @param collect_aux_trace Whether to also map (and eventually read from) the aux buffer.
         * @param token Called once the perf buffer has been registered with this instance, or if a
         * failure has occurred.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_add_ringbuffer(int fd, int cpu, bool collect_aux_trace, CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = shared_from_this(), fd, cpu, collect_aux_trace]() mutable {
                    return start_on(self->strand) //
                         | then([self, fd, cpu, collect_aux_trace]() mutable {
                               // Create the ringbuffer instance if necessary
                               auto buf_it = self->per_cpu_buffers.find(cpu);
                               if (buf_it == self->per_cpu_buffers.end()) {
                                   auto op = detail::perf_consume_op_factory_t(self->strand, fd, cpu, self->config);
                                   if (!op) {
                                       return boost::asio::error::make_error_code(boost::asio::error::invalid_argument);
                                   }

                                   buf_it = self->per_cpu_buffers.emplace(cpu, std::move(op)).first;
                               }
                               else {
                                   // Otherwise just instruct the event FD to output to our ringbuffer
                                   const auto ec = buf_it->second->set_output(fd);
                                   if (ec) {
                                       return ec;
                                   }
                               }

                               if (collect_aux_trace) {
                                   const auto ec = buf_it->second->attach_aux_buffer(fd);
                                   if (ec) {
                                       return ec;
                                   }
                               }

                               return boost::system::error_code {};
                           });
                },
                token);
        }

        /**
         * Overload where no aux data will be collected.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param fd The perf event fd.
         * @param cpu The CPU number that the fd is linked to.
         * @param token Called once the perf buffer has been registered with this instance, or if a
         * failure has occurred.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_add_ringbuffer(int fd, int cpu, CompletionToken && token)
        {
            return async_add_ringbuffer(fd, cpu, false, std::forward<CompletionToken>(token));
        }

        /**
         * Stop tracking for a specific CPU.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param cpu The CPU number that the fd is linked to.
         * @param intermediate_buffer Buffer to dump the ringbuffer data into
         * @param token Called once the perf buffer has been deregistered with this instance, or if a
         * failure has occurred.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_remove_ringbuffer(int cpu,
                                     std::shared_ptr<async::async_buffer_t> intermediate_buffer,
                                     CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = shared_from_this(), cpu, ibuf = std::move(intermediate_buffer)]() mutable {
                    return start_on(self->strand)
                         | then([self, cpu, ibuf = std::move(ibuf)]() mutable
                                -> polymorphic_continuation_t<boost::system::error_code> {
                               auto buf_it = self->per_cpu_buffers.find(cpu);
                               if (buf_it == self->per_cpu_buffers.end()) {
                                   return start_with(
                                       boost::asio::error::make_error_code(boost::asio::error::misc_errors::not_found));
                               }

                               auto buf = std::move(buf_it->second);
                               self->per_cpu_buffers.erase(buf_it);

                               // Drain ringbuffer before destroying
                               return buf->async_send(
                                   detail::data_encode_op_t<data_encoder_type>(self->data_encoder, ibuf),
                                   detail::aux_encode_op_t<aux_encoder_type>(self->aux_encoder, ibuf),
                                   use_continuation);
                           });
                },
                token);
        }

        /**
         * Stop tracking for all CPUs.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param intermediate_buffer Buffer to dump the ringbuffer data into
         * @param token Called once complete.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_remove_all(std::shared_ptr<async::async_buffer_t> intermediate_buffer, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = shared_from_this(), ibuf = std::move(intermediate_buffer)]() mutable {
                    // If there's a failure, we need to report an appropriate error code, but we don't
                    // want to exit early as all other ringbuffers still need removing
                    return start_on(self->strand) //
                         | then([]() { return boost::system::error_code {}; })
                         | loop(
                               [self](auto ec) {
                                   // Even in the event of failure async_remove_ringbuffer will always
                                   // remove the entry in per_cpu_buffers
                                   return start_on(self->strand) //
                                        | then([self, ec]() { return start_with(!self->per_cpu_buffers.empty(), ec); });
                               },
                               [self, ibuf = std::move(ibuf)](auto ec) mutable {
                                   const int cpu = self->per_cpu_buffers.begin()->first;
                                   return self->async_remove_ringbuffer(cpu, ibuf, use_continuation)
                                        | then([ec](auto new_ec) mutable {
                                              if (new_ec) {
                                                  ec = new_ec;
                                              }

                                              return ec;
                                          });
                               });
                },
                token);
        }

        /**
         * Write out data from a per-CPU ringbuffer to the intermediate buffer.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param cpu The CPU number that the fd is linked to.
         * @param intermediate_buffer Buffer to dump the ringbuffer data into
         * @param token Called once complete.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_poll(int cpu, std::shared_ptr<async::async_buffer_t> intermediate_buffer, CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = shared_from_this(), cpu, ibuf = std::move(intermediate_buffer)]() mutable {
                    return start_on(self->strand)
                         | then([self, cpu, ibuf = std::move(ibuf)]() mutable
                                -> polymorphic_continuation_t<boost::system::error_code> {
                               auto buf_it = self->per_cpu_buffers.find(cpu);
                               if (buf_it == self->per_cpu_buffers.end()) {
                                   LOG_DEBUG("No perf buffer for CPU %d", cpu);
                                   return start_with(
                                       boost::asio::error::make_error_code(boost::asio::error::misc_errors::not_found));
                               }

                               return self->async_send(buf_it->second, ibuf, use_continuation);
                           });
                },
                token);
        }

        /**
         * Write out data from all ringbuffers to the intermediate buffer.
         *
         * @tparam CompletionToken Token type, expects an error_code.
         * @param intermediate_buffer Buffer to dump the ringbuffer data into
         * @param token Called once complete.
         * @return Nothing or a continuation, depending on @a CompletionToken
         */
        template<typename CompletionToken>
        auto async_poll_all(std::shared_ptr<async::async_buffer_t> intermediate_buffer, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = shared_from_this(), ibuf = std::move(intermediate_buffer)]() mutable {
                    // If there's a failure, we need to report an appropriate error code, but we don't
                    // want to exit early as all other ringbuffers still need polling
                    return start_on(self->strand) //
                         | then([self]() {
                               return start_with(self->per_cpu_buffers.begin(), boost::system::error_code {});
                           })
                         | loop(
                               [self](auto it, auto ec) {
                                   return start_on(self->strand)
                                        | then([=]() { return start_with(it != self->per_cpu_buffers.end(), it, ec); });
                               },
                               [self, ibuf = std::move(ibuf)](auto it, auto ec) mutable {
                                   return self->async_send(it->second, ibuf, use_continuation)
                                        | then([=](auto new_ec) mutable {
                                              // Even in the event of failure, always increment the iterator
                                              if (new_ec) {
                                                  ec = new_ec;
                                              }
                                              return start_with(++it, ec);
                                          });
                               })
                         | then([](auto /*it*/, auto ec) { return ec; });
                },
                token);
        }

    private:
        using data_encoder_type =
            frame_encoder_t<strand_type, data_record_chunk_tuple_t, encoders::data_record_apc_encoder_t>;
        using aux_encoder_type = frame_encoder_t<strand_type, aux_record_chunk_t, encoders::aux_record_apc_encoder_t>;

        // It's better performance-wise to use unordered_map, but the lack of determinisim of frame order makes
        // testing impossible (no, the default hash has does not order the buckets in ascending order)
        using per_cpu_buffer_type = std::map<int /*cpu*/, std::shared_ptr<detail::perf_consume_op_t<strand_type>>>;

        strand_type strand;
        buffer_config_t config;

        std::shared_ptr<data_encoder_type> data_encoder;
        std::shared_ptr<aux_encoder_type> aux_encoder;

        per_cpu_buffer_type per_cpu_buffers;
    };
}
