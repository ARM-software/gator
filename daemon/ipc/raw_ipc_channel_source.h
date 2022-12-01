/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Logging.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "ipc/codec.h"
#include "ipc/message_key.h"
#include "ipc/message_traits.h"
#include "ipc/messages.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"

#include <cerrno>
#include <memory>
#include <optional>
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
        template<typename R, typename E>
        class sc_wrapper_t {
        public:
            using stored_continuation_type = async::continuations::
                raw_stored_continuation_t<R, E, boost::system::error_code, all_message_types_variant_t>;

            explicit constexpr sc_wrapper_t(stored_continuation_type && sc) : sc(std::move(sc)) {}

            void operator()(boost::asio::io_context & context,
                            bool & recv_in_progress,
                            boost::system::error_code const & ec,
                            all_message_types_variant_t && message)
            {
                // mark receive complete so another may be queued
                recv_in_progress = false;
                // invoke the handler
                resume_continuation(context, std::move(sc), ec, std::move(message));
            };

        private:
            stored_continuation_type sc;
        };

        /** Helper to find the traits type for some key from the list of supported types */
        template<typename... MessageTypes>
        struct message_types_trait_finder_t;

        // Terminator
        template<>
        struct message_types_trait_finder_t<> {
            template<typename T, typename R, typename E>
            static constexpr void visit(message_key_t key, T & host, sc_wrapper_t<R, E> && scw)
            {
                host.do_recv_unknown(key, std::move(scw));
            }
        };

        template<typename MessageType, typename... MessageTypes>
        struct message_types_trait_finder_t<MessageType, MessageTypes...> {
            using next_message_traits_type = message_types_trait_finder_t<MessageTypes...>;
            using traits_type = message_traits_t<MessageType>;

            static_assert(traits_type::key != message_key_t::unknown);

            template<typename T, typename R, typename E>
            static constexpr void visit(message_key_t key, T & host, sc_wrapper_t<R, E> && scw)
            {
                if (key == traits_type::key) {
                    host.template do_recv_known<traits_type>(std::move(scw));
                }
                else {
                    next_message_traits_type::visit(key, host, std::move(scw));
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
            using namespace async::continuations;

            return async_initiate_explicit<void(boost::system::error_code, all_message_types_variant_t)>(
                [st = shared_from_this()](auto && sc) mutable {
                    st->do_async_recv_message(std::forward<decltype(sc)>(sc));
                },
                std::forward<CompletionToken>(token));
        }

    private:
        using message_types_trait_finder_type =
            typename detail::message_types_trait_finder_for_t<all_message_types_variant_t>::type;

        /** Helper wrapper to store the message and encoding buffer */
        template<typename MessageType, typename ReadBufferType>
        using message_wrapper_t = detail::message_wrapper_t<MessageType, ReadBufferType>;

        /** Helper wrapper to hold the handler, and enforce release of 'busy' state */
        template<typename R, typename E>
        using sc_wrapper_t = detail::sc_wrapper_t<R, E>;

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
        template<typename R, typename E>
        void do_async_recv_message(
            async::continuations::
                raw_stored_continuation_t<R, E, boost::system::error_code, all_message_types_variant_t> && sc)
        {
            LOG_TRACE("(%p) New receive request received", this);

            // run on strand to serialize access
            boost::asio::post(strand, [st = shared_from_this(), sc = std::move(sc)]() mutable {
                st->strand_do_async_recv_message(std::move(sc));
            });
        }

        /** Perform the receive action from the strand */
        template<typename R, typename E>
        void strand_do_async_recv_message(
            async::continuations::
                raw_stored_continuation_t<R, E, boost::system::error_code, all_message_types_variant_t> && sc)
        {
            using unknown_message = message_t<message_key_t::unknown, void, void>;
            using key_codec_type = key_codec_t<unknown_message>;
            using sc_wrapper_type = sc_wrapper_t<R, E>;

            // should not already be pending...
            if (std::exchange(recv_in_progress, true)) {
                LOG_TRACE("(%p) Request aborted due to concurrent operation in progress", this);
                using namespace boost::system;
                return resume_continuation(strand.context(),
                                           std::move(sc),
                                           errc::make_error_code(errc::operation_in_progress),
                                           {});
            }

            LOG_TRACE("(%p) Reading next key from stream", this);

            // read the key
            boost::asio::async_read(
                in,
                key_codec_type::mutable_buffer(message_key_buffer),
                [st = shared_from_this(), scw = sc_wrapper_type(std::move(sc))](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading next key failed with error=%s", st.get(), ec.message().c_str());
                        return st->invoke_handler(std::move(scw), ec, {});
                    }

                    // validate size
                    if (n != key_codec_type::key_size) {
                        LOG_TRACE("(%p) Reading next key failed with due to short read (n=%zu)", st.get(), n);
                        return st->invoke_handler(
                            std::move(scw),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // decode the key
                    message_key_t key = st->message_key_buffer;

                    LOG_TRACE("(%p) Reading next key succeeded with new key %zu", st.get(), std::size_t(key));

                    // find the matching traits type
                    return message_types_trait_finder_type::visit(key, *st, std::move(scw));
                });
        }

        /** Received an unexpected key */
        template<typename R, typename E>
        void do_recv_unknown(message_key_t key, sc_wrapper_t<R, E> && scw)
        {
            LOG_TRACE("(%p) Read aborted due to unrecognized message key %zu", this, std::size_t(key));

            using namespace boost::system;
            return invoke_handler(std::move(scw), errc::make_error_code(errc::operation_not_supported), {});
        }

        /** Received a known key and have the type traits for it */
        template<typename TraitsType, typename R, typename E>
        void do_recv_known(sc_wrapper_t<R, E> && scw)
        {
            using traits_type = TraitsType;
            using message_type = typename traits_type::message_type;
            using header_codec_type = header_codec_t<message_type>;
            using suffix_codec_type = suffix_codec_t<message_type>;
            using wrapper_type = message_wrapper_t<message_type, typename suffix_codec_type::sg_read_helper_type>;

            // allocate the message wrapper
            auto message_wrapper = std::make_shared<wrapper_type>();

            // skip reading the header if it is zero length
            if (header_codec_type::header_size == 0) {
                LOG_TRACE("(%p) Skipping header read for key %zu due to zero length header",
                          this,
                          std::size_t(traits_type::key));
                return do_recv_suffix_length<traits_type>(message_wrapper, std::move(scw));
            }

            LOG_TRACE("(%p) Reading header for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      header_codec_type::header_size);

            // read the header
            return boost::asio::async_read(
                in,
                header_codec_type::mutable_buffer(message_wrapper->message),
                [st = shared_from_this(), message_wrapper, scw = std::move(scw)](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading header for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(scw), ec, {});
                    }

                    // validate size
                    if (n != header_codec_type::header_size) {
                        LOG_TRACE("(%p) Reading header for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(scw),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_suffix_length<traits_type>(message_wrapper, std::move(scw));
                });
        }

        /** Received the key and header, now read the length */
        template<typename TraitsType, typename WrapperType, typename R, typename E>
        void do_recv_suffix_length(std::shared_ptr<WrapperType> message_wrapper, sc_wrapper_t<R, E> && scw)
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
                return do_recv_suffix<traits_type>(message_wrapper, std::move(scw));
            }

            LOG_TRACE("(%p) Reading suffix length for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      suffix_codec_type::length_size);

            // read the length
            return boost::asio::async_read(
                in,
                suffix_codec_type::mutable_length_buffer(message_wrapper->buffer),
                [st = shared_from_this(), message_wrapper, scw = std::move(scw)](auto ec, auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading suffix length for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(scw), ec, {});
                    }

                    // validate size
                    if (n != suffix_codec_type::length_size) {
                        LOG_TRACE("(%p) Reading suffix length for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(scw),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_suffix<traits_type>(message_wrapper, std::move(scw));
                });
        }

        /** Received the key, header and length, now read the suffix */
        template<typename TraitsType, typename WrapperType, typename R, typename E>
        void do_recv_suffix(std::shared_ptr<WrapperType> message_wrapper, sc_wrapper_t<R, E> && scw)
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

                return do_recv_complete<traits_type>(message_wrapper, std::move(scw));
            }

            LOG_TRACE("(%p) Reading suffix for key %zu of length %zu",
                      this,
                      std::size_t(traits_type::key),
                      suffix_codec_type::length_size);

            // read the length
            return boost::asio::async_read(
                in,
                buffer,
                [st = shared_from_this(), message_wrapper, scw = std::move(scw), expected_length = buffer.size()](
                    auto ec,
                    auto n) mutable {
                    // validate error
                    if (ec) {
                        LOG_TRACE("(%p) Reading suffix for key=%zu failed with error=%s",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  ec.message().c_str());

                        return st->invoke_handler(std::move(scw), ec, {});
                    }

                    // validate size
                    if (n != expected_length) {
                        LOG_TRACE("(%p) Reading suffix for key=%zu failed with due to short read (n=%zu)",
                                  st.get(),
                                  std::size_t(traits_type::key),
                                  n);

                        return st->invoke_handler(
                            std::move(scw),
                            boost::asio::error::make_error_code(boost::asio::error::misc_errors::eof),
                            {});
                    }

                    // now read the suffix
                    return st->do_recv_complete<traits_type>(message_wrapper, std::move(scw));
                });
        }

        /** Read complete */
        template<typename TraitsType, typename WrapperType, typename R, typename E>
        void do_recv_complete(std::shared_ptr<WrapperType> message_wrapper, sc_wrapper_t<R, E> && scw)
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

                return invoke_handler(std::move(scw), ec, {});
            }

            // notify handler
            return invoke_handler(std::move(scw), ec, std::move(message_wrapper->message));
        }

        /** Invoke the handler */
        template<typename R, typename E>
        void invoke_handler(sc_wrapper_t<R, E> && scw,
                            boost::system::error_code const & ec,
                            all_message_types_variant_t && message)
        {
            scw(strand.context(), recv_in_progress, ec, std::move(message));
        }
    };

    namespace detail {

        template<typename... AllowedTypes>
        struct try_message_filter_t {
            using type_tuple = std::tuple<std::decay_t<AllowedTypes>...>;
            using variant_type = std::variant<std::decay_t<AllowedTypes>...>;
            using pair_type = std::pair<boost::system::error_code, variant_type>;

            template<typename ReceivedType>
            static constexpr std::optional<pair_type> filter(ReceivedType && value)
            {
                using received_type = std::decay_t<ReceivedType>;

                if constexpr (boost::mp11::mp_contains<type_tuple, received_type>::value) {
                    return std::pair {boost::system::error_code {}, variant_type {std::forward<ReceivedType>(value)}};
                }
                else {
                    LOG_DEBUG("Unexpected message [%s]", ipc::named_message_t<received_type>::name.data());
                    return {};
                }
            }
        };

    }

    /**
     * Receive one of a subset of message types from a raw_ipc_channel_source_t. Will continuously
     * receive from the channel, logging and discarding any unwanted messages, util one of the
     * desired types arrives.
     *
     * @tparam MessageTypes The subset of messages to receive.
     * @tparam CompletionToken The asio completion token for this async op.
     */
    template<typename... MessageTypes, typename CompletionToken>
    auto async_receive_one_of(std::shared_ptr<raw_ipc_channel_source_t> source, CompletionToken && token)
    {
        using namespace async::continuations;

        using filter_type = detail::try_message_filter_t<MessageTypes...>;
        using optional_pair_type = std::optional<typename filter_type::pair_type>;
        using variant_type = typename filter_type::variant_type;

        return async_initiate_explicit<void(boost::system::error_code, std::variant<MessageTypes...>)>(
            [src = std::move(source)](auto && sc) {
                submit(
                    start_with(optional_pair_type {}) //
                        | loop(
                            [](optional_pair_type && pair) { return start_with(!pair.has_value(), std::move(pair)); },
                            [src](optional_pair_type const &) {
                                return src->async_recv_message(use_continuation) //
                                     | then([](boost::system::error_code const & ec,
                                               all_message_types_variant_t && msg_variant) -> optional_pair_type {
                                           if (ec) {
                                               return std::pair {ec, variant_type {}};
                                           }

                                           return std::visit(
                                               [](auto && msg) {
                                                   return filter_type::filter(std::forward<decltype(msg)>(msg));
                                               },
                                               std::move(msg_variant));
                                       });
                            })
                        | then([](optional_pair_type && value) { return *value; }) //
                        | unpack_tuple(),
                    std::forward<decltype(sc)>(sc));
            },
            std::forward<CompletionToken>(token));
    }
}
