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
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/system/error_code.hpp>

namespace async {
    /**
     * Helper class for reading lines, one by one from some stream descriptor
     */
    class async_line_reader_t : public std::enable_shared_from_this<async_line_reader_t> {
    public:
        /** Constructor */
        explicit async_line_reader_t(boost::asio::posix::stream_descriptor && sd) : stream_descriptor(std::move(sd)) {}

        /**
         * Read one line from the stream. Completion handler takes (boost::system::error_code, std::string_view).
         * Async completes once per line of text so should be called in a loop.
         * Line of text is delimited by '\n'.
         */
        template<typename CompletionToken>
        auto async_read_line(CompletionToken && token)
        {
            using namespace async::continuations;

            return async_initiate_cont(
                [st = shared_from_this()]() {
                    // consume the bytes from the buffer, ready for the next loop
                    st->buffer.consume(std::exchange(st->n_to_consume, 0));

                    return boost::asio::async_read_until(st->stream_descriptor,
                                                         st->buffer,
                                                         '\n',
                                                         use_continuation) //
                         | then([st](boost::system::error_code const & ec, std::size_t n) {
                               // assumes that data returns a single item
                               static_assert(std::is_same_v<boost::asio::streambuf::const_buffers_type,
                                                            boost::asio::const_buffers_1>);

                               auto const is_eof = (ec == boost::asio::error::eof);

                               // handle errors
                               if ((!is_eof) && ec) {
                                   LOG_DEBUG("Read failed with %s", ec.message().c_str());
                                   return std::pair {ec, std::string_view()};
                               }

                               // process line of text

                               // find the modified buffer chunk
                               auto const input_area = st->buffer.data();
                               auto const read_area_length = std::min(n, input_area.size());

                               // first find the substr containing up-to the first '\n' marker
                               auto const read_area =
                                   std::string_view(reinterpret_cast<char const *>(input_area.data()),
                                                    read_area_length);

                               auto const message = find_end_of_line(read_area);
                               st->n_to_consume = message.size();

                               // only report EOF once buffer is drained of complete lines
                               if (is_eof && message.empty()) {
                                   // if there is some trailing, unterminated (by EOL) text, send it
                                   if (input_area.size() > 0) {
                                       st->n_to_consume = input_area.size();
                                       return std::pair {
                                           boost::system::error_code {},
                                           std::string_view(reinterpret_cast<char const *>(input_area.data()),
                                                            input_area.size())};
                                   }

                                   // otherwise report the EOF
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
        static constexpr std::string_view find_end_of_line(std::string_view chars)
        {
            auto const n = chars.find_first_of('\n');

            return (n != std::string_view::npos ? chars.substr(0, n + 1) //
                                                : std::string_view {});
        }

        boost::asio::posix::stream_descriptor stream_descriptor;
        boost::asio::streambuf buffer {};
        std::size_t n_to_consume {0};
    };

    /**
     * Consume all lines, one by one, from the stream, for each one pass it to the handler
     *
     * @param line_reader The line reader to read from
     * @param handler The handler function of the form `<return>(std::string_view)`, where return may be void or boost::system::error_code, or a continuation thereof
     */
    template<typename Handler, typename CompletionToken>
    auto async_consume_all_lines(std::shared_ptr<async_line_reader_t> line_reader,
                                 Handler && handler,
                                 CompletionToken && token)
    {
        using namespace async::continuations;

        return async_initiate_cont(
            [line_reader = std::move(line_reader), h = std::forward<Handler>(handler)]() mutable {
                return start_with(boost::system::error_code {})                               //
                     | loop([](boost::system::error_code ec) { return start_with(!ec, ec); }, //
                            [line_reader = std::move(line_reader),
                             h = std::move(h)](boost::system::error_code const & /*ec*/) mutable {
                                return line_reader->async_read_line(use_continuation) //
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
                                                              "line consume must return void, error-code or a "
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

    /**
     * Consume all lines, one by one, from the stream, for each one pass it to the handler
     *
     * @param sd The stream to read from
     * @param handler The handler function of the form `<return>(std::string_view)`, where return may be void or boost::system::error_code, or a continuation thereof
     */
    template<typename Handler, typename CompletionToken>
    auto async_consume_all_lines(boost::asio::posix::stream_descriptor && sd,
                                 Handler && handler,
                                 CompletionToken && token)
    {
        return async_consume_all_lines(std::make_shared<async_line_reader_t>(std::move(sd)),
                                       std::forward<Handler>(handler),
                                       std::forward<CompletionToken>(token));
    }
}
