/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/completion_handler.h"
#include "ipc/codec.h"
#include "ipc/message_key.h"
#include "ipc/message_traits.h"
#include "ipc/messages.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"

#include <cerrno>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>

#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_category.hpp>

namespace ipc {

    namespace detail {
        /** Helper wrapper to store the message and encoding buffer */
        template<typename MessageType, typename ReadBufferType>
        struct message_wrapper_t {
            MessageType message;
            ReadBufferType buffer;
        };

        /** Helper wrapper to hold the handler, and enforce release of 'busy' state */
        template<typename Handler>
        class handler_wrapper_t {
        public:
            using handler_type = std::decay_t<Handler>;

            static_assert(std::is_same_v<Handler, handler_type>);

            explicit constexpr handler_wrapper_t(handler_type && handler) : handler(std::forward<handler_type>(handler))
            {
            }

            void operator()(bool & recv_in_progress,
                            boost::system::error_code const & ec,
                            all_message_types_variant_t && message)
            {
                // mark receive complete so another may be queued
                recv_in_progress = false;
                // invoke the handler
                handler(ec, std::move(message));
            };

        private:
            handler_type handler;
        };

        /** Helper to find the traits type for some key from the list of supported types */
        template<typename... MessageTypes>
        struct message_types_trait_finder_t;

        // the terminator must be std::monostate
        template<>
        struct message_types_trait_finder_t<std::monostate> {
            template<typename T, typename Handler>
            static constexpr void visit(message_key_t key, T & host, handler_wrapper_t<Handler> && handler)
            {
                host.do_recv_unknown(key, std::move(handler));
            }
        };

        template<typename MessageType, typename... MessageTypes>
        struct message_types_trait_finder_t<MessageType, MessageTypes...> {
            using next_message_traits_type = message_types_trait_finder_t<MessageTypes...>;
            using traits_type = message_traits_t<MessageType>;

            static_assert(traits_type::key != message_key_t::unknown);

            template<typename T, typename Handler>
            static constexpr void visit(message_key_t key, T & host, handler_wrapper_t<Handler> && handler)
            {
                if (key == traits_type::key) {
                    host.template do_recv_known<traits_type>(std::move(handler));
                }
                else {
                    next_message_traits_type::visit(key, host, std::move(handler));
                }
            }
        };

        /** Helper to generate the correct message_types_trait_finder_t from a variant's tyoe arguments */
        template<typename T>
        struct message_types_trait_finder_for_t;

        template<typename... MessageTypes>
        struct message_types_trait_finder_for_t<std::variant<MessageTypes...>> {
            using type = message_types_trait_finder_t<MessageTypes...>;
        };
    }

    /**
     * The raw read end of an IPC channel. Reads messages from the queue and passes them to the consumer.
     * Should be used as a single consumer loop of the form:
     *     async_read_message(handler) -> handler -> async_read_message...
     */
    class raw_ipc_channel_source_t : public std::enable_shared_from_this<raw_ipc_channel_source_t> {
    public:
        /** Factory method */
        static std::shared_ptr<raw_ipc_channel_source_t> create(boost::asio::io_context & io_context,
                                                                lib::AutoClosingFd && in)
        {
            return std::make_shared<raw_ipc_channel_source_t>(raw_ipc_channel_source_t {io_context, std::move(in)});
        }

        /** Receive the next message */
        template<typename CompletionToken>
        auto async_recv_message(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken,
                                               void(boost::system::error_code, all_message_types_variant_t)>(
                [st = shared_from_this()](auto && handler) mutable {
                    using Handler = decltype(handler);
                    st->do_async_recv_message(std::forward<Handler>(handler));
                },
                token);
        }

    private:
        using message_types_trait_finder_type =
            typename detail::message_types_trait_finder_for_t<all_message_types_variant_t>::type;

        /** Helper wrapper to store the message and encoding buffer */
        template<typename MessageType, typename ReadBufferType>
        using message_wrapper_t = detail::message_wrapper_t<MessageType, ReadBufferType>;

        /** Helper wrapper to hold the handler, and enforce release of 'busy' state */
        template<typename Handler>
        using handler_wrapper_t = detail::handler_wrapper_t<Handler>;

        template<typename... MessageTypes>
        friend struct detail::message_types_trait_finder_t;

        boost::asio::io_context::strand strand;
        boost::asio::posix::stream_descriptor in;
        message_key_t message_key_buffer = message_key_t::unknown;
        bool recv_in_progress = false;

        /** Constructor is hidden to force the use of the factory method since the class is enable_shared_from_this */
        raw_ipc_channel_source_t(boost::asio::io_context & io_context, lib::AutoClosingFd && in)
            : strand(io_context), in(io_context, in.release())
        {
        }

        /** Perform the receive action */
        template<typename Handler>
        void do_async_recv_message(Handler && handler)
        {
            using handler_type = std::decay_t<Handler>;

            LOG_TRACE("(%p) New receive request received", this);

            // run on strand to serialize access
            boost::asio::post(strand,
                              [st = shared_from_this(), handler = std::forward<handler_type>(handler)]() mutable {
                                  st->strand_do_async_recv_message(std::forward<handler_type>(handler));
                              });
        }

        /** Perform the receive action from the strand */
        template<typename Handler>
        void strand_do_async_recv_message(Handler && handler)
        {
            using handler_type = std::decay_t<Handler>;
            using handler_wrapper_type = handler_wrapper_t<handler_type>;

            // should not already be pending...
            if (std::exchange(recv_in_progress, true)) {
                LOG_TRACE("(%p) Request aborted due to concurrent operation in progress", this);
                using namespace boost::system;
                return handler(errc::make_error_code(errc::operation_in_progress), {});
            }

            LOG_TRACE("(%p) Reading next key from stream", this);

            // read the key
            boost::asio::async_read(
                in,
                key_codec_t::mutable_buffer(message_key_buffer),
                [st = shared_from_this(),
                 handler = handler_wrapper_type(std::forward<handler_type>(handler))](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading next key failed with error=%s", st.get(), ec.message().c_str());
                        return st->invoke_handler(std::move(handler), ec, {});
                    }

                    // validate size
                    if (n != key_codec_t::key_size) {
                        LOG_TRACE("(%p) Reading next key failed with due to short read (n=%zu)", st.get(), n);
                        return st->invoke_handler(
                            std::move(handler),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // decode the key
                    message_key_t key = st->message_key_buffer;

                    LOG_TRACE("(%p) Reading next key succeeded with new key %zu", st.get(), std::size_t(key));

                    // find the matching traits type
                    return message_types_trait_finder_type::visit(key, *st, std::move(handler));
                });
        }

        /** Received an unexpected key */
        template<typename Handler>
        void do_recv_unknown(message_key_t key, handler_wrapper_t<Handler> && handler)
        {
            LOG_TRACE("(%p) Read aborted due to unrecognized message key %zu", this, std::size_t(key));

            using namespace boost::system;
            return invoke_handler(std::move(handler), errc::make_error_code(errc::operation_not_supported), {});
        }

        /** Received a known key and have the type traits for it */
        template<typename TraitsType, typename Handler>
        void do_recv_known(handler_wrapper_t<Handler> && handler)
        {
            using handler_type = std::decay_t<Handler>;
            using traits_type = TraitsType;
            using message_type = typename traits_type::message_type;
            using header_codec_type = header_codec_t<message_type>;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using wrapper_type = message_wrapper_t<message_type, typename suffix_codec_type::sg_read_helper_type>;

            static_assert(std::is_same_v<Handler, handler_type>);

            // allocate the message wrapper
            auto message_wrapper = std::make_shared<wrapper_type>();

            // skip reading the header if it is zero length
            if (header_codec_type::header_size == 0) {
                LOG_TRACE("(%p) Skipping header read for key %zu due to zero length header",
                          this,
                          std::size_t(traits_type::key));
                return do_recv_suffix_length<traits_type>(message_wrapper, std::move(handler));
            }

            LOG_TRACE("(%p) Reading header for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      header_codec_type::header_size);

            // read the header
            return boost::asio::async_read(
                in,
                header_codec_type::mutable_buffer(message_wrapper->message),
                [st = shared_from_this(), message_wrapper, handler = std::move(handler)](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading header for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(handler), ec, {});
                    }

                    // validate size
                    if (n != header_codec_type::header_size) {
                        LOG_TRACE("(%p) Reading header for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(handler),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_suffix_length<traits_type>(message_wrapper, std::move(handler));
                });
        }

        /** Received the key and header, now read the length */
        template<typename TraitsType, typename WrapperType, typename Handler>
        void do_recv_suffix_length(std::shared_ptr<WrapperType> message_wrapper, handler_wrapper_t<Handler> && handler)
        {
            using traits_type = TraitsType;
            using message_type = typename traits_type::message_type;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using wrapper_type = message_wrapper_t<message_type, typename suffix_codec_type::sg_read_helper_type>;

            static_assert(std::is_same_v<WrapperType, wrapper_type>);

            // skip reading the length if it is zero length
            if (suffix_codec_type::length_size == 0) {
                LOG_TRACE("(%p) Skipping suffix length read for key %zu due to zero length",
                          this,
                          std::size_t(traits_type::key));
                return do_recv_suffix<traits_type>(message_wrapper, std::move(handler));
            }

            LOG_TRACE("(%p) Reading suffix length for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      suffix_codec_type::length_size);

            // read the length
            return boost::asio::async_read(
                in,
                suffix_codec_type::mutable_length_buffer(message_wrapper->buffer),
                [st = shared_from_this(), message_wrapper, handler = std::move(handler)](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading suffix length for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(handler), ec, {});
                    }

                    // validate size
                    if (n != suffix_codec_type::length_size) {
                        LOG_TRACE("(%p) Reading suffix length for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(handler),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_suffix<traits_type>(message_wrapper, std::move(handler));
                });
        }

        /** Received the key, header and length, now read the suffix */
        template<typename TraitsType, typename WrapperType, typename Handler>
        void do_recv_suffix(std::shared_ptr<WrapperType> message_wrapper, handler_wrapper_t<Handler> && handler)
        {
            using traits_type = TraitsType;
            using message_type = typename traits_type::message_type;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using wrapper_type = message_wrapper_t<message_type, typename suffix_codec_type::sg_read_helper_type>;

            static_assert(std::is_same_v<WrapperType, wrapper_type>);

            auto buffer = suffix_codec_type::mutable_suffix_buffer(message_wrapper->message, message_wrapper->buffer);

            // skip reading the length if it is zero length
            if (buffer.size() == 0) {
                LOG_TRACE("(%p) Skipping suffix read for key %zu due to zero length",
                          this,
                          std::size_t(traits_type::key));

                return do_recv_complete<traits_type>(message_wrapper, std::move(handler));
            }

            LOG_TRACE("(%p) Reading suffix for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      suffix_codec_type::length_size);

            // read the length
            return boost::asio::async_read(
                in,
                buffer,
                [st = shared_from_this(),
                 message_wrapper,
                 handler = std::move(handler),
                 expected_length = buffer.size()](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading suffix for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(handler), ec, {});
                    }

                    // validate size
                    if (n != expected_length) {
                        LOG_TRACE("(%p) Reading suffix for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(handler),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_complete<traits_type>(message_wrapper, std::move(handler));
                });
        }

        /** Read complete */
        template<typename TraitsType, typename WrapperType, typename Handler>
        void do_recv_complete(std::shared_ptr<WrapperType> message_wrapper, handler_wrapper_t<Handler> && handler)
        {
            using traits_type = TraitsType;
            using message_type = typename traits_type::message_type;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using wrapper_type = message_wrapper_t<message_type, typename suffix_codec_type::sg_read_helper_type>;

            static_assert(std::is_same_v<WrapperType, wrapper_type>);

            LOG_TRACE("(%p) Reading complete for key %zu", this, std::size_t(traits_type::key));

            // apply conversion from read buffer to message suffix
            auto ec = suffix_codec_type::read_suffix(message_wrapper->buffer, message_wrapper->message);

            // handle conversion failure
            if (ec) {
                LOG_TRACE("(%p) Decode suffix failed for key %zu due to error=%s",
                          this,
                          std::size_t(traits_type::key),
                          ec.message().c_str());

                return invoke_handler(std::move(handler), ec, {});
            }

            // notify handler
            return invoke_handler(std::move(handler), ec, std::move(message_wrapper->message));
        }

        /** Invoke the handler */
        template<typename Handler>
        void invoke_handler(handler_wrapper_t<Handler> && handler,
                            boost::system::error_code const & ec,
                            all_message_types_variant_t && message)
        {
            handler(recv_in_progress, ec, std::move(message));
        }
    };

    /**
     * An async operation that receives from the source channel until one of the requested
     * message types arrives. Any unrequested message types are logged and discarded.
     */
    template<typename CompletionHandler, typename... MessageTypes>
    class receive_one_of_op {
        using type_tuple = std::tuple<MessageTypes...>;

    public:
        explicit receive_one_of_op(std::shared_ptr<raw_ipc_channel_source_t> channel, CompletionHandler && handler)
            : handler {std::forward<CompletionHandler>(handler)}, channel(std::move(channel))
        {
        }

        void operator()(const boost::system::error_code & ec, all_message_types_variant_t && msg_variant)
        {
            if (ec) {
                handler(ec, std::variant<MessageTypes...> {});
                return;
            }

            std::visit([this](auto && msg) { this->try_message_filter(std::forward<decltype(msg)>(msg)); },
                       msg_variant);
        }

    private:
        using handler_type =
            async::completion_handler_ref_t<const boost::system::error_code &, std::variant<MessageTypes...>>;
        handler_type handler;
        std::shared_ptr<raw_ipc_channel_source_t> channel;

        template<typename MessageType>
        void try_message_filter(MessageType && msg)
        {
            using T = std::decay_t<MessageType>;
            if constexpr (boost::mp11::mp_contains<type_tuple, T>::value) {
                handler({}, std::variant<MessageTypes...>(std::forward<MessageType>(msg)));
            }
            else {
                LOG_DEBUG("Unexpected message [%s]", ipc::named_message_t<T>::name.data());

                channel->async_recv_message(std::move(*this));
            }
        }
    };

    /**
     * Receive one of a subset of message types from a raw_ipc_channel_source_t. Will continuously
     * receive from the channel, logging and discarding any unwanted messages, util one of the
     * desired types arrives. The completion handler should expect to receive a variant that could
     * also contain std::monostate.
     *
     * @tparam MessageTypes The subset of messages to receive.
     * @tparam CompletionToken The asio completion token for this async op.
     */
    template<typename... MessageTypes, typename CompletionToken>
    auto async_receive_one_of(std::shared_ptr<raw_ipc_channel_source_t> source, CompletionToken && token)
    {
        return boost::asio::async_initiate<CompletionToken,
                                           void(boost::system::error_code,
                                                std::variant<std::monostate, MessageTypes...>)>(
            [src = std::move(source)](auto && handler) {
                using HandlerType = decltype(handler);
                auto op = receive_one_of_op<HandlerType, std::monostate, MessageTypes...> {
                    src,
                    std::forward<HandlerType>(handler)};
                src->async_recv_message(std::move(op));
            },
            token);
    }
}
