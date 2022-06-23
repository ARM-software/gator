/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "ipc/codec.h"
#include "ipc/message_key.h"
#include "ipc/message_traits.h"
#include "ipc/responses.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"

#include <deque>
#include <type_traits>

#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

namespace ipc {
    /**
     * The raw write end of an IPC channel
     */
    class raw_ipc_channel_sink_t : public std::enable_shared_from_this<raw_ipc_channel_sink_t> {
    public:
        template<typename R, typename E, typename M>
        using stored_message_continuation_t =
            async::continuations::raw_stored_continuation_t<R, E, boost::system::error_code, M>;

        /** Factory method */
        static std::shared_ptr<raw_ipc_channel_sink_t> create(boost::asio::io_context & io_context,
                                                              lib::AutoClosingFd && out)
        {
            return std::make_shared<raw_ipc_channel_sink_t>(raw_ipc_channel_sink_t {io_context, std::move(out)});
        }

        /**
         * Write some fixed-size message into the send buffer.
         */
        template<typename MessageType, typename CompletionToken>
        auto async_send_message(MessageType message, CompletionToken && token)
        {
            using namespace async::continuations;

            using message_type = std::decay_t<MessageType>;

            static_assert(is_ipc_message_type_v<message_type>);

            return async_initiate_explicit<void(boost::system::error_code, message_type)>(
                [st = shared_from_this(), message = std::forward<message_type>(message)](auto && sc) mutable {
                    st->do_async_send_message(std::forward<MessageType>(message), std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Write some fixed-size message into the send buffer.
         */
        template<typename MessageType, typename CompletionToken>
        auto async_send_response(MessageType message, CompletionToken && token)
        {
            using namespace async::continuations;
            using message_type = std::decay_t<MessageType>;
            static_assert(is_response_message_type_v<message_type>);

            return async_initiate_explicit<void(boost::system::error_code, message_type)>(
                [st = shared_from_this(), message = std::forward<message_type>(message)](auto && sc) mutable {
                    st->do_async_send_message(std::forward<MessageType>(message), std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        /** Type erasing base class for queue items allowing any type of message or handler to be supported */
        class message_queue_item_base_t {
        public:
            virtual ~message_queue_item_base_t() noexcept = default;
            [[nodiscard]] virtual std::size_t expected_size() const = 0;
            virtual void do_send(raw_ipc_channel_sink_t & parent,
                                 std::shared_ptr<message_queue_item_base_t> shared_this) = 0;
            virtual void call_handler(boost::asio::io_context & context, boost::system::error_code const & ec) = 0;
        };

        /** Default message queue item type, copies the message into a buffer object held in the queue item */
        template<typename MessageType, typename R, typename E>
        class message_queue_item_t : public message_queue_item_base_t {
        public:
            using message_type = std::decay_t<MessageType>;
            using key_codec_type = key_codec_t<message_type>;
            using header_codec_type = header_codec_t<message_type>;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using sg_write_helper_type = typename suffix_codec_type::sg_write_helper_type;
            using stored_continuation_t = stored_message_continuation_t<R, E, message_type>;

            constexpr message_queue_item_t(message_type && message, stored_continuation_t && sc)
                : message(std::move(message)),
                  sg_helper(suffix_codec_type::fill_sg_write_helper_type(this->message)),
                  sc(std::move(sc))
            {
            }

            [[nodiscard]] std::size_t expected_size() const override
            {
                return key_codec_type::key_size + header_codec_type::header_size
                     + suffix_codec_type::suffix_write_size(sg_helper);
            }

            void do_send(raw_ipc_channel_sink_t & parent,
                         std::shared_ptr<message_queue_item_base_t> shared_this) override
            {
                using sg_buffer_type =
                    std::array<boost::asio::const_buffer,
                               key_codec_type::sg_writer_buffers_count + header_codec_type::sg_writer_buffers_count
                                   + suffix_codec_type::sg_writer_buffers_count>;

                // fill the scatter gather buffer list
                sg_buffer_type buffers {};
                lib::Span<boost::asio::const_buffer> buffers_span {buffers};

                key_codec_type::fill_sg_buffer(buffers_span.subspan(0, key_codec_type::sg_writer_buffers_count),
                                               message_type::key);
                header_codec_type::fill_sg_buffer(buffers_span.subspan(key_codec_type::sg_writer_buffers_count,
                                                                       header_codec_type::sg_writer_buffers_count),
                                                  message);
                suffix_codec_type::fill_sg_buffer(buffers_span.subspan(key_codec_type::sg_writer_buffers_count
                                                                       + header_codec_type::sg_writer_buffers_count),
                                                  sg_helper);

                // pass it  to the parent for sending
                parent.do_send_item(std::move(shared_this), std::move(buffers));
            }

            void call_handler(boost::asio::io_context & context, boost::system::error_code const & ec) override
            {
                resume_continuation(context, std::move(sc), ec, std::move(message));
            }

        private:
            message_type message;
            sg_write_helper_type sg_helper;
            stored_continuation_t sc;
        };

        // so it can call do_send_item
        template<typename MessageType, typename R, typename E>
        friend class message_queue_item_t;

        boost::asio::io_context::strand strand;
        boost::asio::posix::stream_descriptor out;
        std::deque<std::shared_ptr<message_queue_item_base_t>> send_queue {};
        bool consume_in_progress = false;

        /** Constructor is hidden to force the use of the factory method since the class is enable_shared_from_this */
        raw_ipc_channel_sink_t(boost::asio::io_context & io_context, lib::AutoClosingFd && out)
            : strand(io_context), out(io_context, out.release())
        {
        }

        /** Insert the message and handler into the send queue */
        template<typename MessageType, typename R, typename E>
        void do_async_send_message(MessageType && message,
                                   stored_message_continuation_t<R, E, std::decay_t<MessageType>> && sc)
        {
            using message_type = std::decay_t<MessageType>;
            using queue_item_t = message_queue_item_t<message_type, R, E>;

            LOG_TRACE("(%p) New send request received with key %zu", this, std::size_t(message_type::key));

            // run on the strand to serialize access to the queue
            boost::asio::post(strand,
                              [st = shared_from_this(),
                               queue_item = std::make_shared<queue_item_t>(std::forward<MessageType>(message),
                                                                           std::move(sc))]() mutable {
                                  st->strand_do_async_send_message(message_type::key, std::move(queue_item));
                              });
        }

        /** Insert the message and handler into the send queue */
        template<typename MessageType>
        void strand_do_async_send_message(MessageType key, std::shared_ptr<message_queue_item_base_t> queue_item)
        {
            // fast path for case that queue is already empty and consumer is waiting
            const auto cip = is_consume_in_progress();
            if (send_queue.empty() && !cip) {
                return strand_do_consume_item(std::move(queue_item));
            }

            LOG_TRACE("(%p) Queueing new request %p with key %zu (empty=%u, busy=%u)",
                      this,
                      queue_item.get(),
                      std::size_t(key),
                      send_queue.empty(),
                      cip);

            // stick it in the queue, the consumer will pick it up when its ready
            send_queue.emplace_back(std::move(queue_item));
        }

        /** Consume data from the buffer and write to stream */
        void strand_do_consume_item(std::shared_ptr<message_queue_item_base_t> && queue_item)
        {
            // NB: must already be on the strand
            runtime_assert(queue_item != nullptr, "Invalid queue item");

            // mark busy to prevent another send request from enqueueing up in parallel
            const auto cip = set_consume_in_progress(true);
            LOG_TRACE("(%p) Consuming queue item %p (empty=%u, busy=%u)",
                      this,
                      queue_item.get(),
                      send_queue.empty(),
                      cip);

            runtime_assert(!cip, "Invalid state");

            // call the do_send method, which will then invoke the do_send_item with one or more buffers to actually send
            queue_item->do_send(*this, queue_item);
        }

        /** Called by the message queue item to send the actual data */
        template<std::size_t N>
        void do_send_item(std::shared_ptr<message_queue_item_base_t> && queue_item,
                          std::array<boost::asio::const_buffer, N> buffers)
        {
            // NB: must already be on the strand

            LOG_TRACE("(%p) Sending queue item %p (n_buffers=%zu)", this, queue_item.get(), N);

            // perform the actual write
            boost::asio::async_write(out,
                                     buffers,
                                     [st = shared_from_this(), queue_item = std::move(queue_item)](
                                         boost::system::error_code const & ec,
                                         std::size_t n) mutable { st->on_sent_result(queue_item, ec, n); });
        }

        /** Handle the send result */
        void on_sent_result(std::shared_ptr<message_queue_item_base_t> const & queue_item,
                            boost::system::error_code const & ec,
                            std::size_t n)
        {
            //  error
            if (ec) {
                LOG_DEBUG("(%p) Sending queue item %p failed with error=%s",
                          this,
                          queue_item.get(),
                          ec.message().c_str());
                // notify the handler (which happens asynchronously)
                return queue_item->call_handler(strand.context(), ec);
            }

            //  short write error
            if (n != queue_item->expected_size()) {
                LOG_DEBUG("(%p) Sending queue item %p failed with short write %zu", this, queue_item.get(), n);
                // notify the handler (which happens asynchronously)
                return queue_item->call_handler(
                    strand.context(),
                    boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof));
            }

            // notify the handler (which happens asynchronously)
            queue_item->call_handler(strand.context(), {});

            // consume the next item (but from the stand as it will modify state)
            return boost::asio::post(strand, [st = shared_from_this()]() { st->strand_do_consume_next(); });
        }

        /** Consume the next item (running on the strand) */
        void strand_do_consume_next()
        {
            LOG_TRACE("(%p) Request to process next queue item", this);

            // send is complete
            auto cip = set_consume_in_progress(false);
            runtime_assert(cip, "Invalid state");

            // nothing to do?
            if (send_queue.empty()) {
                LOG_TRACE("(%p) Queue is empty", this);
                return;
            }

            // remove the head of the senq queue
            auto next_item = std::move(send_queue.front());
            send_queue.pop_front();

            // and send it
            return strand_do_consume_item(std::move(next_item));
        }

        /** Check if consume in progress */
        bool is_consume_in_progress() const { return consume_in_progress; }
        /** Change consume in progress flag */
        bool set_consume_in_progress(bool cip) { return std::exchange(consume_in_progress, cip); }
    };
}
