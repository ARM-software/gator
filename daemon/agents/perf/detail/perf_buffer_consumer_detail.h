/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "agents/perf/record_types.h"
#include "async/async_buffer.hpp"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "k/perf_event.h"
#include "lib/Span.h"
#include "lib/Syscall.h"

#include <array>
#include <functional>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

#include <sys/mman.h>

namespace agents::perf::detail {

    constexpr auto error_buf_sz = 256;

    struct buffer_region_t {
        std::uint64_t head {0};
        std::uint64_t tail {0};
    };

    /**
     * Holds the captured state of the data & aux buffer pointers so that the kernel can continue
     * writing into the buffer while we're waiting for asynchronous processing to complete.
     */
    struct buffer_snapshot_t {
        perf_event_mmap_page * header_page;
        buffer_region_t data;
        buffer_region_t aux;
    };

    /**
     * An async operation that will encode & write a sequence of data_record_chunk_tuple_t into
     * the intermediate buffer.
     */
    template<typename data_encoder_type>
    class data_encode_op_t {
    public:
        data_encode_op_t(std::shared_ptr<data_encoder_type> encoder,
                         std::shared_ptr<async::async_buffer_t> async_buffer)
            : encoder {std::move(encoder)}, async_buffer(std::move(async_buffer))
        {
        }

        template<typename CompletionToken>
        auto async_exec(int cpu, lib::Span<data_record_chunk_tuple_t> chunks, CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [cpu, chunks, e = encoder, b = async_buffer]() mutable {
                    return start_with() //
                         | then([cpu, chunks, e, b = std::move(b)]() mutable {
                               return e->async_encode(std::move(b), cpu, 0, chunks, use_continuation);
                           }) //
                         | then([](auto ec, auto /*n*/) { return start_with(ec); });
                },
                token);
        }

    private:
        std::shared_ptr<data_encoder_type> encoder;
        std::shared_ptr<async::async_buffer_t> async_buffer;
    };

    /**
     * An async operation that will encode & write a sequence of aux_record_chunk_t into the
     * intermediate buffer.
     */
    template<typename aux_encoder_type>
    class aux_encode_op_t {
    public:
        aux_encode_op_t(std::shared_ptr<aux_encoder_type> encoder, std::shared_ptr<async::async_buffer_t> async_buffer)
            : encoder {std::move(encoder)}, async_buffer {std::move(async_buffer)}
        {
        }

        template<typename CompletionToken>
        auto async_exec(int cpu, std::uint64_t tail, lib::Span<aux_record_chunk_t> chunks, CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [cpu, tail, chunks, e = encoder, b = async_buffer]() mutable {
                    return start_with() //
                         | then([cpu, tail, chunks, e, b = std::move(b)]() mutable {
                               return e->async_encode(std::move(b), cpu, tail, chunks, use_continuation);
                           }) //
                         | then([](auto ec, auto /*n*/) { return start_with(ec); });
                },
                token);
        }

    private:
        std::shared_ptr<aux_encoder_type> encoder;
        std::shared_ptr<async::async_buffer_t> async_buffer;
    };

    /**
     * Encapsulates the logic to parse the perf aux buffer into an array of aux_record_chunk_t
     * and pass that span to an async consumer. Once that consumer completes this operation's
     * completion handler will update the ring buffer tail pointer to give the space back to
     * the kernel.
     *
     * @tparam SnapshotFn Callable that returns a buffer snapshot.
     * @tparam ComposedOp A callable that holds the async operation that will process
     *  the parsed records.
     */
    template<typename SnapshotFn, typename ComposedOp>
    class aux_consume_op_t : public std::enable_shared_from_this<aux_consume_op_t<SnapshotFn, ComposedOp>> {
    public:
        explicit aux_consume_op_t(int cpu,
                                  perf_buffer_t * perf_buffer,
                                  std::size_t aux_buffer_length,
                                  SnapshotFn && snap,
                                  ComposedOp && op)
            : cpu(cpu),
              perf_buffer(perf_buffer),
              aux_buffer_length(aux_buffer_length),
              snapper(std::forward<SnapshotFn>(snap)),
              op(std::forward<ComposedOp>(op))
        {
        }

        template<typename CompletionToken>
        auto async_exec(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate<continuation_of_t<boost::system::error_code, buffer_snapshot_t>>(
                [self = this->shared_from_this()]() mutable {
                    return start_with() //
                         | then([self]() -> polymorphic_continuation_t<boost::system::error_code, buffer_snapshot_t> {
                               const char * buffer = static_cast<char *>(self->perf_buffer->aux_buffer);

                               const auto snapshot = self->snapper();

                               const auto header_head = snapshot.aux.head;
                               const auto header_tail = snapshot.aux.tail;

                               if (header_head <= header_tail) {
                                   return start_with(boost::system::error_code {}, snapshot);
                               }

                               const auto length = self->aux_buffer_length;

                               const std::size_t buffer_mask = length - 1;

                               // will be 'length' at most otherwise somehow wrapped many times
                               const std::size_t total_data_size =
                                   std::min<uint64_t>(header_head - header_tail, length);
                               const std::uint64_t head = header_head;
                               // will either be the same as 'tail' or will be > if somehow wrapped multiple times
                               const std::uint64_t tail = (header_head - total_data_size);

                               const std::size_t tail_masked = (tail & buffer_mask);
                               const std::size_t head_masked = (head & buffer_mask);

                               const bool have_wrapped = head_masked < tail_masked;

                               const std::size_t first_size = (have_wrapped ? (length - tail_masked) : total_data_size);
                               const std::size_t second_size = (have_wrapped ? head_masked : 0);

                               if (first_size <= 0) {
                                   self->update_buffer_position(snapshot);
                                   return start_with(boost::system::error_code {}, snapshot);
                               }

                               self->chunks = {aux_record_chunk_t {buffer + tail_masked, first_size},
                                               aux_record_chunk_t {buffer, second_size}};

                               return self->op.async_exec(self->cpu,
                                                          tail,
                                                          self->chunks,
                                                          use_continuation) //
                                    | then([self, snapshot](auto ec) {
                                          self->update_buffer_position(snapshot);
                                          return start_with(ec, snapshot);
                                      });
                           });
                },
                token);
        }

    private:
        int cpu;
        perf_buffer_t * perf_buffer;
        const std::size_t aux_buffer_length;
        SnapshotFn snapper;
        ComposedOp op;
        std::array<aux_record_chunk_t, 2> chunks;

        void update_buffer_position(buffer_snapshot_t snapshot)
        {
            // only update if we're actually consuming from the aux buffer
            if (snapshot.aux.head != snapshot.aux.tail) {
                // Update tail with the aux read and synchronize with the buffer writer
                __atomic_store_n(&snapshot.header_page->aux_tail, snapshot.aux.head, __ATOMIC_RELEASE);
            }
        }
    };

    /**
     * An async operation that parses arrays of data_record_chunk_tuple_t from the perf event
     * ring buffer, and passes those arrays to an async consumer for further processing.
     * Parsing is done in fixed sized blocks of chunks (CHUNK_BUFFER_SIZE) and will loop until
     * the snapshotted ring buffer region has been consumed. Once that has happend the tail pointer
     * is updated, to pass the buffer space back to the kernel, and the completion hander is called.
     *
     * @tparam Executor The Asio exectuor to use when dispatching intermediate operations.
     * @tparam ComposedOp The async consumer that will processed the parsed data_record_chunk_tuple_t
     *  arrays.
     */
    template<typename Executor, typename ComposedOp>
    class data_consume_op_t : public std::enable_shared_from_this<data_consume_op_t<Executor, ComposedOp>> {

    private:
        // arbitrary, roughly 4k size stack allocation on 64-bit
        static constexpr std::size_t CHUNK_BUFFER_SIZE = 256;
        static constexpr std::size_t CHUNK_WORD_SIZE = sizeof(data_word_t);

        std::reference_wrapper<Executor> executor;
        int cpu;
        char * ring_buffer;
        const std::size_t buffer_length;
        const std::size_t buffer_mask;
        buffer_snapshot_t snap;
        ComposedOp op;

        std::array<data_record_chunk_tuple_t, CHUNK_BUFFER_SIZE> chunk_buffer;
        std::size_t head;
        std::size_t tail;

        template<typename T>
        const T * ring_buffer_ptr(const char * base, std::size_t position_masked)
        {
            return reinterpret_cast<const T *>(base + position_masked);
        }

        template<typename T>
        const T * ring_buffer_ptr(const char * base, std::size_t position, std::size_t size_mask)
        {
            return ring_buffer_ptr<T>(base, position & size_mask);
        }

        std::size_t calculate_next_chunk_count()
        {
            // start by clearning any old junk out of the buffer
            std::size_t num_chunks_in_buffer = 0;

            // is there any more work left to do?
            while ((head > tail) && (num_chunks_in_buffer != CHUNK_BUFFER_SIZE)) {
                const auto * record_header = ring_buffer_ptr<perf_event_header>(ring_buffer, tail, buffer_mask);
                const auto record_size = (record_header->size + CHUNK_WORD_SIZE - 1) & ~(CHUNK_WORD_SIZE - 1);
                const auto record_end = tail + record_size;
                const std::size_t base_masked = (tail & buffer_mask);
                const std::size_t end_masked = (record_end & buffer_mask);

                const bool have_wrapped = end_masked < base_masked;

                const std::size_t first_size = (have_wrapped ? (buffer_length - base_masked) : record_size);
                const std::size_t second_size = (have_wrapped ? end_masked : 0);

                // set chunk
                chunk_buffer[num_chunks_in_buffer].first_chunk.chunk_pointer =
                    ring_buffer_ptr<data_word_t>(ring_buffer, base_masked);
                chunk_buffer[num_chunks_in_buffer].first_chunk.word_count = first_size / CHUNK_WORD_SIZE;
                chunk_buffer[num_chunks_in_buffer].optional_second_chunk.chunk_pointer =
                    ring_buffer_ptr<data_word_t>(ring_buffer, 0);
                chunk_buffer[num_chunks_in_buffer].optional_second_chunk.word_count = second_size / CHUNK_WORD_SIZE;

                num_chunks_in_buffer += 1;
                tail = record_end;
            }

            return num_chunks_in_buffer;
        }

        async::continuations::polymorphic_continuation_t<boost::system::error_code> co_initiate()
        {
            using namespace async::continuations;
            return start_on<on_executor_mode_t::post>(executor.get()) //
                 | then([self = this->shared_from_this()]() mutable
                        -> polymorphic_continuation_t<boost::system::error_code> {
                       auto num_chunks_in_buffer = self->calculate_next_chunk_count();
                       // if there's nothing left in the buffer then our work is done - call the completion handler
                       if (num_chunks_in_buffer == 0) {
                           return start_with(boost::system::error_code {});
                       }

                       return self->op.async_exec(self->cpu,
                                                  lib::Span<data_record_chunk_tuple_t> {self->chunk_buffer.data(),
                                                                                        num_chunks_in_buffer},
                                                  use_continuation) //
                            | then([self](auto ec) -> polymorphic_continuation_t<boost::system::error_code> {
                                  // something went wrong so just drop this part of the
                                  // buffer and let the handler know
                                  if (ec) {
                                      return start_with(ec);
                                  }
                                  // otherwise, loop back round and continue reading from the ring buffer
                                  return self->co_initiate();
                              });
                   });
        }

        void update_buffer_position()
        {
            __atomic_store_n(&snap.header_page->data_tail, snap.data.head, __ATOMIC_RELEASE);
        }

    public:
        /**
         * Constructs the reader "closure" over the specified state data.
         */
        data_consume_op_t(Executor & executor,
                          int cpu,
                          char * ring_buffer,
                          std::size_t buffer_length,
                          const buffer_snapshot_t & snap,
                          ComposedOp && op)
            : executor(executor),
              cpu(cpu),
              ring_buffer(ring_buffer),
              buffer_length(buffer_length),
              buffer_mask(buffer_length - 1),
              snap(snap),
              op(std::move(op)),
              chunk_buffer {},
              head(snap.data.head),
              tail(snap.data.tail)
        {
        }

        template<typename CompletionToken>
        auto async_exec(CompletionToken && token)
        {
            using namespace async::continuations;
            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = this->shared_from_this()]() mutable {
                    return self->co_initiate() //
                         | then([self](auto ec) {
                               self->update_buffer_position();
                               return ec;
                           });
                },
                token);
        }
    };

    /**
     * Calculate the mmap region from @a config.
     *
     * @param config Buffer config.
     * @return Size in bytes.
     */
    [[nodiscard]] inline std::size_t get_data_mmap_length(const buffer_config_t & config)
    {
        return config.page_size + config.data_buffer_size;
    }

    /**
     * Attempts to create the ringbuffer mmap and provides detailed logging
     * upon error.
     *
     * @param cpu CPU ID.
     * @param config Buffer config.
     * @param length mmap size in bytes.
     * @param offset Offset into @fd to load, bytes in multiples of pages.
     * @param fd File descriptor to load.
     * @return MMapped region start pointer, or MAP_FAILED on error.
     */
    [[nodiscard]] void * try_mmap_with_logging(int cpu,
                                               const buffer_config_t & config,
                                               std::size_t length,
                                               off_t offset,
                                               int fd);

    /**
     * An encapsulation of the logic to asynchronously process the data + aux buffers for a single CPU.
     */
    template<typename Executor>
    class perf_consume_op_t : public std::enable_shared_from_this<perf_consume_op_t<Executor>> {
    public:
        /**
         * Construct the async op for the specified CPU ring buffer.
         *
         * @param executor The Asio executor to use when dispatching async operations.
         * @param cpu The cpu number that this buffer is attached to.
         * @param config The config object that holds buffer length & page size information.
         * @param pb An object that holds pointers to the data & aux buffers.
         */
        perf_consume_op_t(Executor & executor, int cpu, const buffer_config_t & config, perf_buffer_t pb)
            : executor(executor),
              cpu(cpu),
              config(config),
              header_page(reinterpret_cast<perf_event_mmap_page *>(static_cast<char *>(pb.data_buffer))),
              perf_buffer(pb)
        {
        }

        /** Unmaps the mmaped regions. */
        ~perf_consume_op_t()
        {
            lib::munmap(perf_buffer.data_buffer, get_data_mmap_length(config));
            if (perf_buffer.aux_buffer != nullptr) {
                lib::munmap(perf_buffer.aux_buffer, config.aux_buffer_size);
            }
        }

        /**
         * Asynchronously calls the aux and then data consumers.
         *
         * @tparam DataConsumeOp Data consumer type
         * @tparam AuxConsumeOp Aux consumer type
         * @tparam CompletionToken Token type, expects an error_code.
         * @param data_op Data consumer
         * @param aux_op Aux consumer
         * @param token Called once the operations have completed
         */
        template<typename DataConsumeOp, typename AuxConsumeOp, typename CompletionToken>
        auto async_send(DataConsumeOp && data_op, AuxConsumeOp && aux_op, CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = this->shared_from_this(),
                 data_op = std::forward<DataConsumeOp>(data_op),
                 aux_op = std::forward<AuxConsumeOp>(aux_op)]() mutable {
                    return start_on(self->executor) //
                         | then([self, aux_op = std::move(aux_op)]() mutable {
                               auto snapshotter = [self]() { return self->snapshot(); };

                               auto consumer = std::make_shared<aux_consume_op_t<decltype(snapshotter), AuxConsumeOp>>(
                                   self->cpu,
                                   &(self->perf_buffer),
                                   self->config.aux_buffer_size,
                                   std::move(snapshotter),
                                   std::move(aux_op));

                               return consumer->async_exec(use_continuation);
                           })
                         | then([self, data_op = std::move(data_op)](auto ec, auto snap) mutable
                                -> polymorphic_continuation_t<boost::system::error_code> {
                               if (ec) {
                                   return start_with(ec);
                               }

                               auto consumer = std::make_shared<data_consume_op_t<Executor, DataConsumeOp>>(
                                   self->executor,
                                   self->cpu,
                                   static_cast<char *>(self->perf_buffer.data_buffer) + self->config.page_size,
                                   self->config.data_buffer_size,
                                   snap,
                                   std::move(data_op));

                               return consumer->async_exec(use_continuation);
                           });
                },
                token);
        }

        /**
         * Calls ioctl with PERF_EVENT_IOC_SET_OUTPUT on @fd using the buffer's
         * FD.
         *
         * @param fd FD of event to assign the output of.
         * @return True on success.
         */
        boost::system::error_code set_output(int fd)
        {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            if (lib::ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, perf_buffer.fd) < 0) {
                std::array<char, error_buf_sz> strbuf {0};
                strerror_r(errno, strbuf.data(), strbuf.size());
                LOG_DEBUG("ioctl failed for fd %i (errno=%d, %s)", fd, errno, strbuf.data());
                return boost::system::errc::make_error_code(static_cast<boost::system::errc::errc_t>(errno));
            }

            return {};
        }

        /**
         * Create an aux buffer mmap and associate it with this instance.
         *
         * No-op if an aux buffer is alerady attached.
         * @param fd File descriptor to mmap.
         * @return True on success.
         */
        boost::system::error_code attach_aux_buffer(int fd)
        {
            if (perf_buffer.aux_buffer == nullptr) {
                const auto offset = get_data_mmap_length(config);
                const auto length = config.aux_buffer_size;

                perf_event_mmap_page & pemp = *static_cast<perf_event_mmap_page *>(perf_buffer.data_buffer);
                pemp.aux_offset = offset;
                pemp.aux_size = length;

                if (offset > std::numeric_limits<off_t>::max()) {
                    LOG_DEBUG("Offset for perf aux buffer is out of range: %zu", offset);
                    return boost::system::errc::make_error_code(boost::system::errc::result_out_of_range);
                }

                auto * buf = try_mmap_with_logging(cpu, config, length, static_cast<off_t>(offset), fd);
                // NOLINTNEXTLINE(performance-no-int-to-ptr)
                if (buf == MAP_FAILED) {
                    // Can't use errno here as other ops in try_mmap_with_logging overrride it
                    return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
                }

                perf_buffer.aux_buffer = buf;
                if (perf_buffer.aux_fd > 0) {
                    LOG_DEBUG("Multiple aux fds");
                    return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
                }
                perf_buffer.aux_fd = fd;
            }

            return {};
        }

    private:
        Executor & executor;
        int cpu;
        const buffer_config_t & config;
        perf_event_mmap_page * header_page;
        perf_buffer_t perf_buffer;

        /**
         * Creates a point-in-time snapshot of the state of the ring buffer head/tail pointers.
         * This allows us to process the buffer asynchronously whilst the kernel continues write
         * into it. We need to ensure that we don't publish aux buffer entries before the data
         * records.
         */
        [[nodiscard]] buffer_snapshot_t snapshot() const
        {
            buffer_snapshot_t snap;

            // We read the data buffer positions before we read the aux buffer positions
            // so that we never send records more recent than the aux
            snap.header_page = header_page;
            snap.data.head = __atomic_load_n(&snap.header_page->data_head, __ATOMIC_ACQUIRE);
            // Only we write this so no atomic load needed
            snap.data.tail = snap.header_page->data_tail;

            // Now send the aux data before the records to ensure the consumer never receives
            // a PERF_RECORD_AUX without already having received the aux data
            void * const aux_buf = perf_buffer.aux_buffer;
            if (aux_buf != nullptr) {
                snap.aux.head = __atomic_load_n(&snap.header_page->aux_head, __ATOMIC_ACQUIRE);
                // Only we write this so no atomic load needed
                snap.aux.tail = snap.header_page->aux_tail;
            }

            return snap;
        }
    };

    /**
     * Creates a perf_consume_op_t once it's primary ringbuffer has been
     * successfully initialised.
     *
     * @tparam Executor Executor type.
     * @param executor Excecutor instance.
     * @param fd MMap FD.
     * @param cpu CPU index.
     * @param config Buffer configuration.
     * @return perf_consume_op_t shared pointer, or nullptr if mmap-ing was
     * unsuccessful.
     */
    template<typename Executor>
    [[nodiscard]] std::shared_ptr<perf_consume_op_t<Executor>> perf_consume_op_factory_t(Executor & executor,
                                                                                         int fd,
                                                                                         int cpu,
                                                                                         const buffer_config_t & config)
    {
        // Create the data buffer instance
        auto * buf = detail::try_mmap_with_logging(cpu, config, get_data_mmap_length(config), 0, fd);
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        if (buf == MAP_FAILED) {
            return {};
        }

        // Check the version
        perf_event_mmap_page & pemp = *static_cast<perf_event_mmap_page *>(buf);
        const auto compat_version = pemp.compat_version;
        if (compat_version != 0) {
            LOG_DEBUG("Incompatible perf_event_mmap_page compat_version (%i) for fd %i", compat_version, fd);
            lib::munmap(buf, get_data_mmap_length(config));
            return {};
        }

        return std::make_shared<perf_consume_op_t<Executor>>(executor,
                                                             cpu,
                                                             config,
                                                             perf_buffer_t {buf, nullptr, fd, -1});
    }
}
