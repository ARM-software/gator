/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "ISender.h"
#include "async/completion_handler.h"
#include "lib/memory_pool.h"

#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

namespace async {

    // default the buffer size to fit the biggest possible frame
    constexpr std::size_t default_memory_pool_size = ISender::MAX_RESPONSE_LENGTH;

    /**
     * An asynchronous producer/consumer buffer with fixed (but configurable) size.
     *
     * Producers may request some space within the buffer, which may be fulfilled asynchronously:
     * - if space is available, the request may complete directly
     * - otherwise, the request is added to a queue and completes as the space is made free by some consumer operation
     *
     * A single consumer will asynchronously wait for data to be available in the buffer. When it becomes available the
     * consumer is called with one of the buffers to send. Once the send is complete, the consumer should reregister in order
     * to receiver another (by async_consume).
     *
     * The producer is passed a 'commit_action_t' object which it will use to notify the buffer that it has completed writing
     * to its allocated space. It can also use this object to discard the buffer (should it need to). The object will discard
     * on destruction if not previously commited. The commit method can take an optional consumer token of the form 'void(bool)'
     * which allows the producer to register for notification that the data was sent. The bool argument will be true on
     * successful send, and false otherwise.
     *
     * Likewise, the consumer is passed a 'consume_action_t' object which it must invoke only once the data has been fully
     * consumed. If the object goes out of scope it will automatically mark the space as consumed, so the consumer must ensure
     * the proper life of said object. The 'consume_action_t::consume()' method takes an optional bool argument (which defaults
     * to true). This value is passed to the producer's notification token (if one was passed) to notify that the send
     * was successful (or not). If the object goes out of scope before consume is called it will be notified that the send
     * was not successful.
     *
     * Both commit_action_t and consume_action_t are move-only types.
     *
     */
    class async_buffer_t {
    public:
        class commit_action_t;
        class consume_action_t;

        using const_buffer_type = lib::Span<const char>;
        using mutable_buffer_type = lib::Span<char>;
        using sent_completion_handler_type = completion_handler_ref_t<bool>;
        using wait_for_space_handler_type = completion_handler_ref_t<bool, mutable_buffer_type, commit_action_t>;

    private:
        struct reclaim_entry_t {
            reclaim_entry_t(lib::alloc::memory_pool_t::pointer_type allocation, bool ready)
                : allocation(std::move(allocation)), ready(ready)
            {
            }

            lib::alloc::memory_pool_t::pointer_type allocation;
            bool ready;
        };

        struct wait_for_space_entry_t {
            wait_for_space_entry_t(std::size_t n, wait_for_space_handler_type handler)
                : n(n), handler(std::move(handler))
            {
            }

            std::size_t n;
            wait_for_space_handler_type handler;
        };

        struct wait_for_commit_entry_t {
            wait_for_commit_entry_t(std::size_t n, reclaim_entry_t & reclaim_entry, const_buffer_type buffers)
                : n(n), reclaim_entry(reclaim_entry), buffers(buffers)
            {
            }

            std::size_t n;
            reclaim_entry_t & reclaim_entry;
            const_buffer_type buffers;
        };

        struct ready_to_send_entry_t {
            ready_to_send_entry_t(std::size_t n,
                                  reclaim_entry_t & reclaim_entry,
                                  const_buffer_type buffers,
                                  sent_completion_handler_type handler)
                : n(n), reclaim_entry(reclaim_entry), buffers(buffers), handler(std::move(handler))
            {
            }

            std::size_t n;
            reclaim_entry_t & reclaim_entry;
            const_buffer_type buffers;
            sent_completion_handler_type handler;
        };

        struct sending_entry_t {
            sending_entry_t(std::size_t n, reclaim_entry_t & reclaim_entry, sent_completion_handler_type handler)
                : n(n), reclaim_entry(reclaim_entry), handler(std::move(handler))
            {
            }

            std::size_t n;
            reclaim_entry_t & reclaim_entry;
            sent_completion_handler_type handler;
        };

        struct one_shot_t {
            template<typename Handler>
            explicit one_shot_t(Handler && h) : running_total {0}, handler {std::forward<Handler>(h)}
            {
            }

            std::size_t running_total;
            completion_handler_ref_t<boost::system::error_code> handler;
        };

        using wait_for_commit_iter_t = std::list<wait_for_commit_entry_t>::iterator;
        using wait_for_consume_iter_t = std::list<sending_entry_t>::iterator;

    public:
        /**
         * Passed to the 'request space'' completion handler, provides a call back to mark the region as being commited or discarded.
         */
        class commit_action_t {
        public:
            constexpr commit_action_t() = default;

            // move only
            commit_action_t(commit_action_t const &) = delete;
            commit_action_t & operator=(commit_action_t const &) = delete;

            commit_action_t(commit_action_t && that) noexcept
                : parent(std::exchange(that.parent, nullptr)), entry(std::exchange(that.entry, {}))
            {
            }

            commit_action_t & operator=(commit_action_t && that) noexcept
            {
                if (this != &that) {
                    commit_action_t temp {std::move(that)};
                    std::swap(parent, temp.parent);
                    std::swap(entry, temp.entry);
                }
                return *this;
            }

            ~commit_action_t() noexcept
            {
                // discard the entry (if commit was not called)
                discard();
            }

            void commit(sent_completion_handler_type && handler_ref = {})
            {
                boost::system::error_code ec;
                if (!commit(ec, entry->n, std::move(handler_ref))) {
                    throw std::runtime_error("Buffer commit failed: " + ec.message());
                }
            }

            /** Mark the buffer region as commited and ready to send */
            [[nodiscard]] bool commit(boost::system::error_code & ec,
                                      std::size_t size,
                                      sent_completion_handler_type && handler_ref = {})
            {
                if (size > entry->n) {
                    ec = boost::system::errc::make_error_code(boost::system::errc::value_too_large);
                    return false;
                }
                auto * p = std::exchange(parent, nullptr);
                auto e = std::exchange(entry, {});

                if ((p != nullptr) && (e != wait_for_commit_iter_t {})) {
                    p->commit_entry(e, size, std::move(handler_ref));
                }

                return true;
            }

            /** Mark the buffer region as discarded */
            void discard()
            {
                auto * p = std::exchange(parent, nullptr);
                auto e = std::exchange(entry, {});

                if ((p != nullptr) && (e != wait_for_commit_iter_t {})) {
                    p->discard_entry(e);
                }
            }

        private:
            friend class async_buffer_t;

            constexpr commit_action_t(async_buffer_t & parent, wait_for_commit_iter_t const & entry)
                : parent(&parent), entry(entry)
            {
            }

            async_buffer_t * parent = nullptr;
            wait_for_commit_iter_t entry {};
        };

        /**
         * Passed to the 'consume' completion handler, provides a call back to mark the region as being consumed.
         */
        class consume_action_t {
        public:
            constexpr consume_action_t() = default;

            // move only
            consume_action_t(consume_action_t const &) = delete;
            consume_action_t & operator=(consume_action_t const &) = delete;

            consume_action_t(consume_action_t && that) noexcept
                : parent(std::exchange(that.parent, nullptr)), entry(std::exchange(that.entry, {}))
            {
            }

            consume_action_t & operator=(consume_action_t && that) noexcept
            {
                if (this != &that) {
                    consume_action_t temp {std::move(that)};
                    std::swap(parent, temp.parent);
                    std::swap(entry, temp.entry);
                }
                return *this;
            }

            ~consume_action_t() noexcept
            {
                // consume the entry (if not already done so)
                consume(false);
            }

            /** Mark the buffer region as consumed */
            void consume(bool success = true)
            {
                auto * p = std::exchange(parent, nullptr);
                auto e = std::exchange(entry, {});

                if ((p != nullptr) && (e != wait_for_consume_iter_t {})) {
                    p->consume_entry(e, success);
                }
            }

        private:
            friend class async_buffer_t;

            constexpr consume_action_t(async_buffer_t & parent, wait_for_consume_iter_t const & entry)
                : parent(&parent), entry(entry)
            {
            }

            async_buffer_t * parent = nullptr;
            wait_for_consume_iter_t entry = {};
        };

        /** Constructor */
        explicit async_buffer_t(boost::asio::io_context & io_context,
                                std::size_t maximum_size = default_memory_pool_size)
            : strand(io_context), mem_pool(maximum_size)
        {
        }

        /** Destructor */
        ~async_buffer_t()
        {
            // If there's an outstanding one-shot mode handler, then invoke it
            // as cancelled so we don't make any client async process 'stuck'
            boost::asio::post(strand, [osm = std::move(one_shot_mode)]() mutable {
                if (osm) {
                    osm->handler(boost::asio::error::operation_aborted);
                }
            });
        }

        /** Enable one-shot mode, the completion handler will be invoked when
         * the total committed bytes is equal to or greater than the pool size.
         */
        template<typename CompletionToken>
        auto async_buffer_full_oneshot(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
                [this](auto && handler) mutable {
                    using Handler = decltype(handler);

                    boost::asio::dispatch(strand, [this, h = std::forward<Handler>(handler)]() mutable {
                        one_shot_mode = one_shot_t {std::move(h)};
                    });
                },
                token);
        }

        /** Request some data to send */
        template<typename CompletionToken>
        auto async_consume(CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(bool, const_buffer_type, consume_action_t)>(
                [this](auto && handler) mutable {
                    using Handler = decltype(handler);
                    do_async_consume(std::forward<Handler>(handler));
                },
                token);
        }

        /** Request some fixed space in the buffer */
        template<typename CompletionToken>
        auto async_request_space(std::size_t n, CompletionToken && token)
        {
            return boost::asio::async_initiate<CompletionToken, void(bool, mutable_buffer_type, commit_action_t)>(
                [this](auto && handler, std::size_t n) mutable {
                    using Handler = decltype(handler);
                    this->do_async_request_space(std::forward<Handler>(handler), n);
                },
                token,
                n);
        }

    private:
        friend class commit_action_t;
        friend class consume_action_t;

        using pending_send_action_t = completion_handler_ref_t<bool, const_buffer_type, consume_action_t>;

        /** @return a const version of the mutable buffer */
        static const_buffer_type as_const_buffer(mutable_buffer_type const & b)
        {
            return const_buffer_type(b.data(), b.size());
        }

        boost::asio::io_context::strand strand;
        lib::alloc::memory_pool_t mem_pool;
        std::optional<one_shot_t> one_shot_mode;
        pending_send_action_t pending_send_action {};

        std::list<reclaim_entry_t> reclaim_queue {};
        std::list<wait_for_space_entry_t> waiting_for_space_queue {};
        std::list<wait_for_commit_entry_t> waiting_for_commit_queue {};
        std::list<ready_to_send_entry_t> ready_to_send_queue {};
        std::list<sending_entry_t> sending_queue {};

        /** Perform the async_consume action */
        template<typename Handler>
        void do_async_consume(Handler && handler)
        {
            static_assert(!std::is_reference_v<Handler>);
            static_assert(!std::is_const_v<Handler>);

            // ok, execute on strand to serialize access to the buffer and queue
            boost::asio::dispatch(strand, [this, handler = std::forward<Handler>(handler)]() mutable {
                // not allowed to have multiple senders...
                if (pending_send_action) {
                    return handler(false, const_buffer_type {}, consume_action_t {});
                }

                // save the handler
                pending_send_action = std::forward<Handler>(handler);

                // check for stuff in the queue
                check_for_sendable_items();
            });
        }

        /** Perform the async_request_space action */
        template<typename Handler>
        void do_async_request_space(Handler && handler, std::size_t n)
        {
            static_assert(!std::is_reference_v<Handler>);
            static_assert(!std::is_const_v<Handler>);

            // fail if n == 0
            if (n == 0) {
                return handler(false, mutable_buffer_type {}, commit_action_t {});
            }

            // ok, execute on strand to serialize access to the buffer and queue
            boost::asio::dispatch(strand, [this, handler = std::forward<Handler>(handler), n]() mutable {
                // if the request is greater than the total pool size it'll never be fulfilled
                if (n > mem_pool.size()) {
                    return handler(false, mutable_buffer_type {}, commit_action_t {});
                }

                auto data = mem_pool.alloc(n);

                if (data) {
                    auto mutable_buffer = mutable_buffer_type {data->data(), data->size()};
                    auto & reclaim_entry = reclaim_queue.emplace_back(std::move(data), false);
                    auto iter = waiting_for_commit_queue.insert(
                        waiting_for_commit_queue.end(),
                        wait_for_commit_entry_t {n, reclaim_entry, as_const_buffer(mutable_buffer)});

                    return handler(true, std::move(mutable_buffer), commit_action_t(*this, iter));
                }
                else {
                    waiting_for_space_queue.emplace_back(n, std::forward<Handler>(handler));
                    return;
                }
            });
        }

        /** Handle commit of some entry in the queue */
        void commit_entry(wait_for_commit_iter_t const & iter,
                          std::size_t commit_size,
                          sent_completion_handler_type && handler_ref)
        {
            // run on the strand, to serialize access
            boost::asio::dispatch(strand, [this, iter, commit_size, handler_ref = std::move(handler_ref)]() mutable {
                // convert the entry to ready
                ready_to_send_queue.emplace_back(commit_size,
                                                 iter->reclaim_entry,
                                                 iter->buffers,
                                                 std::move(handler_ref));

                // remove from wait queue
                waiting_for_commit_queue.erase(iter);

                // and notify the send wait object if there is one
                check_for_sendable_items();

                // check if one-shot mode is enabled and if we have hit the buffer size
                check_one_shot_mode(commit_size);
            });
        }

        /** Handle discard of some entry in the queue */
        void discard_entry(wait_for_commit_iter_t const & iter)
        {
            // run on the strand, to serialize access
            boost::asio::dispatch(strand, [this, iter]() {
                // mark the reclaim entry as ready
                iter->reclaim_entry.ready = true;
                // remove from wait queue
                waiting_for_commit_queue.erase(iter);
                // check the reclaim queue
                check_for_reclaim_items();
            });
        }

        /** Handle consume of some sent entry in the queue */
        void consume_entry(wait_for_consume_iter_t const & iter, bool success)
        {
            // run on the strand, to serialize access
            boost::asio::dispatch(strand, [this, iter, success]() {
                // enqueue to notify the handler
                if (iter->handler) {
                    boost::asio::post(strand,
                                      [handler = std::move(iter->handler), success]() mutable { handler(success); });
                }

                // mark the reclaim entry as ready
                iter->reclaim_entry.ready = true;
                // remove from wait queue
                sending_queue.erase(iter);
                // check the reclaim queue
                check_for_reclaim_items();
            });
        }

        /** Check the ready-to-send queue for any items */
        void check_for_sendable_items()
        {
            // NB: must be called from the strand

            // nothing to do if the consumer is missing or the queue is empty
            if ((!pending_send_action) || ready_to_send_queue.empty()) {
                return;
            }

            // remove the item from the RTS queue and insert in the SEND queue
            ready_to_send_entry_t queue_entry = std::move(ready_to_send_queue.front());
            ready_to_send_queue.pop_front();
            auto iter = sending_queue.insert(
                sending_queue.end(),
                sending_entry_t {queue_entry.n, queue_entry.reclaim_entry, std::move(queue_entry.handler)});

            // just send the first item in the queue
            boost::asio::post(
                strand,
                [this, buffer = const_buffer_type(queue_entry.buffers.data(), queue_entry.n), iter]() mutable {
                    if (pending_send_action) {
                        pending_send_action(true, buffer, consume_action_t {*this, iter});
                    }
                });
        }

        /** Check the reclaim list for ready items and reclaim the space back into the buffer for reuse */
        void check_for_reclaim_items()
        {
            // NB: must be called from the strand
            bool reclaimed_space = false;

            // first reclaim the space
            for (auto iter = reclaim_queue.begin(); iter != reclaim_queue.end();) {
                // the reclaimed memory must be returned to the buffer in order (since we do not track the spaces individually)
                if (!iter->ready) {
                    break;
                }

                // remove from list. the allocation will be freed in the destructor
                iter = reclaim_queue.erase(iter);

                // so we know to check the wait for space list
                reclaimed_space = true;
            }

            if (!reclaimed_space) {
                return;
            }

            // arbitrary limit to 2g
            std::size_t smallest_failed_alloc_attempt = std::numeric_limits<int32_t>::max();

            // now check for anything waiting for space
            for (auto iter = waiting_for_space_queue.begin(); iter != waiting_for_space_queue.end();) {
                // if we've already made an allocation attempt that was smaller than this and it
                // failed then there's no point trying this one
                if (iter->n >= smallest_failed_alloc_attempt) {
                    iter++;
                    continue;
                }

                // try to allocate a contiguous region
                auto data = mem_pool.alloc(iter->n);

                if (data) {
                    // we got some memory so queue up the wait_for_commit entry and invoke the handler
                    auto mutable_buffer = mutable_buffer_type {data->data(), data->size()};
                    auto handler = std::move(iter->handler);
                    auto & reclaim_entry = reclaim_queue.emplace_back(reclaim_entry_t {std::move(data), false});
                    auto commit_iter = waiting_for_commit_queue.insert(
                        waiting_for_commit_queue.end(),
                        wait_for_commit_entry_t {iter->n, reclaim_entry, as_const_buffer(mutable_buffer)});

                    // now remove the old item
                    iter = waiting_for_space_queue.erase(iter);

                    // invoke the handler asynchronously
                    boost::asio::post(strand,
                                      [handler = std::move(handler),
                                       mutable_buffer,
                                       action = commit_action_t(*this, commit_iter)]() mutable {
                                          handler(true, mutable_buffer, std::move(action));
                                      });
                }
                else {
                    // not enough space for this allocation. record this as a failed attempt
                    // and carry on
                    smallest_failed_alloc_attempt = iter->n;
                    ++iter;
                }
            }
        }

        void check_one_shot_mode(std::size_t commit_bytes)
        {
            // NB: must be called from the strand

            if (one_shot_mode) {
                one_shot_mode->running_total += commit_bytes;
                if (one_shot_mode->running_total >= mem_pool.size()) {
                    boost::asio::post(strand, [osm = std::move(one_shot_mode)]() mutable { osm->handler({}); });

                    // Lovely C++ quirk here, the optional has to be manually reset because moving from it doesn't clear
                    // the value, it does a move-from _on_ the value.  In our case that just moves the handler but
                    // doesn't reset the optional, which leads to a segfault in the lambda...
                    one_shot_mode.reset();
                }
            }
        }
    };
}
