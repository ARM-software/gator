/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/continuation_of.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"

#include <memory>
#include <string_view>
#include <type_traits>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/system/error_code.hpp>

namespace async {

    /**
     * Helper class for reading chunks of byte data from some stream descriptor, repeatedly until eof
     */
    class async_byte_reader_t : public std::enable_shared_from_this<async_byte_reader_t> {
    public:
        static constexpr std::size_t default_read_chunk_size = 65536;

        // assumes that data returns a single item
        static_assert(std::is_same_v<boost::asio::streambuf::const_buffers_type, boost::asio::const_buffers_1>);

        /** Constructor */
        explicit async_byte_reader_t(boost::asio::posix::stream_descriptor && sd,
                                     std::size_t read_chunk_size = default_read_chunk_size)
            : stream_descriptor(std::move(sd)), read_chunk_size(read_chunk_size)
        {
        }

        /**
         * Read one chunk from the stream. Completion handler takes (boost::system::error_code, std::string_view).
         * Async completes once per chunk of bytes so should be called in a loop.
         */
        template<typename CompletionToken>
        auto async_read_some(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate(
                [st = shared_from_this()]() {
                    // consume the bytes from the buffer, ready for the next loop
                    st->buffer.consume(std::exchange(st->n_to_consume, 0));

                    return st->stream_descriptor.async_read_some(st->buffer.prepare(st->read_chunk_size),
                                                                 use_continuation) //
                         | then([st](boost::system::error_code const & ec, std::size_t n) {
                               auto const is_eof = (ec == boost::asio::error::eof);

                               // handle errors
                               if ((!is_eof) && ec) {
                                   LOG_DEBUG("Read failed with %s", ec.message().c_str());
                                   return std::pair {ec, std::string_view()};
                               }

                               // commit output->input
                               st->buffer.commit(n);

                               // extract the string view from the buffer
                               auto const input_area = st->buffer.data();
                               auto const message = std::string_view(reinterpret_cast<char const *>(input_area.data()),
                                                                     input_area.size());

                               st->n_to_consume = message.size();

                               // only report the EOF once the buffer is empty
                               if (is_eof && message.empty()) {
                                   return std::pair {boost::system::error_code {boost::asio::error::eof},
                                                     std::string_view {}};
                               }

                               // report the line
                               return std::pair {boost::system::error_code {}, message};
                           }) //
                         | unpack_tuple();
                },
                std::forward<CompletionToken>(token));
        }

    private:
        boost::asio::posix::stream_descriptor stream_descriptor;
        boost::asio::streambuf buffer {};
        std::size_t read_chunk_size;
        std::size_t n_to_consume {0};
    };

    template<typename Handler, typename CompletionToken>
    auto async_consume_all_bytes(std::shared_ptr<async_byte_reader_t> pipe_reader,
                                 Handler && handler,
                                 CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate(
            [pipe_reader = std::move(pipe_reader), h = std::forward<Handler>(handler)]() mutable {
                return start_with(boost::system::error_code {})                               //
                     | loop([](boost::system::error_code ec) { return start_with(!ec, ec); }, //
                            [pipe_reader = std::move(pipe_reader),
                             h = std::move(h)](boost::system::error_code const & /*ec*/) mutable {
                                return pipe_reader->async_read_some(use_continuation) //
                                     | then([&h](boost::system::error_code ec, std::string_view message)
                                                -> polymorphic_continuation_t<boost::system::error_code> {
                                           // exit loop early on error
                                           if (ec) {
                                               return start_with(ec);
                                           }

                                           // pass message to handler and consume result
                                           return start_with(message) //
                                                | then(h)             //
                                                | then([](auto... args) {
                                                      using args_type =
                                                          continuation_of_t<std::decay_t<decltype(args)>...>;

                                                      if constexpr (std::is_same_v<continuation_of_t<>, args_type>) {
                                                          return boost::system::error_code {};
                                                      }
                                                      else {
                                                          static_assert(
                                                              std::is_same_v<
                                                                  continuation_of_t<boost::system::error_code>,
                                                                  args_type>,
                                                              "Pipe consume must return void, error-code or a "
                                                              "continuation thereof");

                                                          return boost::system::error_code {args...};
                                                      }
                                                  });
                                       });
                            })
                     // filter EOF
                     | then([](boost::system::error_code ec) {
                           if (ec != boost::asio::error::eof) {
                               return ec;
                           }
                           return boost::system::error_code {};
                       }) //
                     | map_error();
            },
            std::forward<CompletionToken>(token));
    }

    template<typename Handler, typename CompletionToken>
    auto async_consume_all_bytes(boost::asio::posix::stream_descriptor && sd,
                                 Handler && handler,
                                 CompletionToken && token)
    {
        return async_consume_all_bytes(std::make_shared<async_byte_reader_t>(std::move(sd)),
                                       std::forward<Handler>(handler),
                                       std::forward<CompletionToken>(token));
    }
}
