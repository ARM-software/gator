/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "agents/perf/events/perf_ringbuffer_mmap.hpp"
#include "agents/perf/events/types.hpp"
#include "agents/perf/record_types.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/stored_continuation.h"
#include "async/continuations/use_continuation.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "lib/Assert.h"
#include "lib/EnumUtils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

namespace agents::perf {

    /**
     * Monitors a set of file descriptors, and maintains a polling timer such that whenever an FD is readable, or whenever the timer fires, one or more of the associated data buffers will be flushed into the capture
     *
     * @tparam PerfActivator The perf_activator_t type, used to reenable aux fds
     * @tparam PerfBufferConsumer The perf_buffer_consumer_t type
     * @tparam StreamDescriptor The file descriptor type
     */
    template<typename PerfActivator, typename PerfBufferConsumer, typename StreamDescriptor>
    class async_perf_ringbuffer_monitor_t
        : public std::enable_shared_from_this<
              async_perf_ringbuffer_monitor_t<PerfActivator, PerfBufferConsumer, StreamDescriptor>> {
    public:
        using perf_activator_t = PerfActivator;
        using perf_buffer_consumer_t = PerfBufferConsumer;
        using stream_descriptor_t = StreamDescriptor;
        using fd_aux_flag_pair_t = std::pair<std::shared_ptr<stream_descriptor_t>, bool>;

        static constexpr auto live_poll_interval = std::chrono::milliseconds(100);
        static constexpr auto local_poll_interval = std::chrono::seconds(1);

        async_perf_ringbuffer_monitor_t(boost::asio::io_context & context,
                                        std::shared_ptr<ipc::raw_ipc_channel_sink_t> const & ipc_sink,
                                        std::shared_ptr<perf_activator_t> const & perf_activator,
                                        bool live_mode,
                                        std::size_t one_shot_mode_limit)
            : timer(context),
              strand(context),
              perf_activator(perf_activator),
              perf_buffer_consumer(std::make_shared<perf_buffer_consumer_t>(context, ipc_sink, one_shot_mode_limit)),
              live_mode(live_mode)
        {
        }

        async_perf_ringbuffer_monitor_t(boost::asio::io_context & context,
                                        std::shared_ptr<perf_activator_t> const & perf_activator,
                                        std::shared_ptr<perf_buffer_consumer_t> perf_buffer_consumer,
                                        bool live_mode)
            : timer(context),
              strand(context),
              perf_activator(perf_activator),
              perf_buffer_consumer(std::move(perf_buffer_consumer)),
              live_mode(live_mode)
        {
        }

        /** Is the monitor was requested to terminate */
        [[nodiscard]] bool is_terminate_requested() const { return terminate_requested; }
        /** Is the monitor terminated */
        [[nodiscard]] bool is_terminate_completed() const { return terminate_complete; }

        /** Start the polling timer */
        void start_timer() { do_start_timer(); }

        /** Terminate the monitor */
        void terminate()
        {
            using namespace async::continuations;

            LOG_TRACE("Terminating...");

            spawn("stop perf event monitor",
                  start_on(strand) //
                      | then([st = this->shared_from_this()]() -> polymorphic_continuation_t<> {
                            st->terminate_requested = true;
                            st->timer.cancel();

                            for (auto & stream : st->primary_streams) {
                                boost::system::error_code ignored {};
                                stream->close(ignored);
                            }

                            for (auto & stream : st->supplimentary_streams) {
                                boost::system::error_code ignored {};
                                stream->close(ignored);
                            }

                            if (st->primary_streams.empty() && st->supplimentary_streams.empty()) {
                                // If there are no monitored streams then the termination_handler will never be called, so
                                // call the remove processing directly
                                return st->async_try_poll();
                            }

                            return {};
                        }));
        }

        /**
         * Wait for notification that the required number of bytes is sent in one-shot mode
         * NB: will never notify if one-shot mode is disabled
         */
        template<typename CompletionToken>
        auto async_wait_one_shot_full(CompletionToken && token)
        {
            return perf_buffer_consumer->async_wait_one_shot_full(std::forward<CompletionToken>(token));
        }

        /**
         * Add a new ring buffer to the set of monitored ringbuffers
         */
        template<typename CompletionToken>
        auto async_add_ringbuffer(int cpu,
                                  std::vector<fd_aux_flag_pair_t> primary_fds,
                                  std::vector<fd_aux_flag_pair_t> supplimentary_fds,
                                  std::shared_ptr<perf_ringbuffer_mmap_t> mmap,
                                  CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("async_add_ringbuffer(%d, %zu, %zu, %p)",
                      cpu,
                      primary_fds.size(),
                      supplimentary_fds.size(),
                      mmap->header());

            return async_initiate_cont(
                [st = this->shared_from_this(),
                 primary_fds = std::move(primary_fds),
                 supplimentary_fds = std::move(supplimentary_fds),
                 mmap = std::move(mmap),
                 cpu]() mutable {
                    return start_on(st->strand) //
                         | then([st, cpu]() {
                               // should not already be tracked?
                               runtime_assert(st->cpu_fd_counter.find(cpu) == st->cpu_fd_counter.end(),
                                              "a mmap is already tracked");
                           }) //
                         | st->perf_buffer_consumer->async_add_ringbuffer(cpu,
                                                                          std::move(mmap),
                                                                          use_continuation) //
                         | map_error()                                                      //
                         | post_on(st->strand)                                              //
                         | then([st,
                                 primary_fds = std::move(primary_fds),
                                 supplimentary_fds = std::move(supplimentary_fds),
                                 cpu]() {
                               for (const auto & pair : primary_fds) {
                                   st->spawn_observer_perf_fd(cpu, pair.first, true, pair.second);
                               }
                               for (const auto & pair : supplimentary_fds) {
                                   st->spawn_observer_perf_fd(cpu, pair.first, false, pair.second);
                               }
                           });
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Add a new ring buffer to the set of monitored ringbuffers
         */
        void add_additional_event_fds(std::vector<std::pair<core_no_t, fd_aux_flag_pair_t>> primary_fds,
                                      std::vector<std::pair<core_no_t, fd_aux_flag_pair_t>> supplimentary_fds)
        {
            LOG_TRACE("add_additional_event_fds(%zu, %zu)", primary_fds.size(), supplimentary_fds.size());
            for (auto const & pair : primary_fds) {
                spawn_observer_perf_fd(lib::toEnumValue(pair.first), pair.second.first, true, pair.second.second);
            }
            for (auto const & pair : supplimentary_fds) {
                spawn_observer_perf_fd(lib::toEnumValue(pair.first), pair.second.first, false, pair.second.second);
            }
        }

        /**
         * Wait for a specific mmap to be removed
         */
        template<typename CompletionToken>
        auto await_mmap_removed(int cpu, CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("await_mmap_removed(%d)", cpu);

            return async_initiate_explicit<void()>(
                [st = this->shared_from_this(), cpu](auto && sc) mutable {
                    submit(start_on(st->strand) //
                               | then([st, sc = sc.move(), cpu]() mutable {
                                     // is it already not tracked, just let the continuation know
                                     auto it = st->cpu_fd_counter.find(cpu);
                                     if (it == st->cpu_fd_counter.end()) {
                                         LOG_TRACE("mmap %d is already removed", cpu);
                                         return resume_continuation(st->strand.context(), std::move(sc));
                                     }

                                     // store it for later
                                     auto res = st->cpu_shutdown_monitors.try_emplace(cpu, std::move(sc));

                                     // shouldn't be two handlers
                                     runtime_assert(!res.second, "Can't register two mmap removal handlers");
                                 }),
                           sc.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

        /**
         * Wait for the ringbuffer to be fully terminated (i.e terminate is requested, and all buffers are removed and fully drained)
         */
        template<typename CompletionToken>
        auto async_wait_terminated(CompletionToken && token)
        {
            using namespace async::continuations;

            LOG_TRACE("async_wait_terminated()");

            return async_initiate_explicit<void()>(
                [st = this->shared_from_this()](auto && sc) mutable {
                    submit(start_on(st->strand) //
                               | then([st, sc = sc.move()]() mutable {
                                     // if already terminated, just let the continuation know
                                     if (st->terminate_complete) {
                                         LOG_TRACE("already terminated");
                                         return resume_continuation(st->strand.context(), std::move(sc));
                                     }

                                     // shouldn't be two
                                     runtime_assert(!st->termination_handler,
                                                    "Can't register two termination handlers");

                                     // store it for later
                                     st->termination_handler = std::move(sc);
                                 }),
                           sc.get_exceptionally());
                },
                std::forward<CompletionToken>(token));
        }

    private:
        boost::asio::steady_timer timer;
        boost::asio::io_context::strand strand;
        std::shared_ptr<perf_activator_t> perf_activator;
        std::shared_ptr<perf_buffer_consumer_t> perf_buffer_consumer;
        // pending_cpus is split into a read and write list so that the poll/remove loop
        // doesn't get blocked from calling the remove part of the loop by
        // cpu-nos being repeatedly added to the pending list.
        std::array<std::deque<int>, 2> pending_cpus_rw {};
        std::deque<int> * pending_cpus_read {&pending_cpus_rw[0]};
        std::deque<int> * pending_cpus_write {&pending_cpus_rw[1]};
        std::array<std::map<int, std::set<std::shared_ptr<stream_descriptor_t>>>, 2> cpu_aux_streams_rw {};
        std::map<int, std::set<std::shared_ptr<stream_descriptor_t>>> * cpu_aux_streams_read {&cpu_aux_streams_rw[0]};
        std::map<int, std::set<std::shared_ptr<stream_descriptor_t>>> * cpu_aux_streams_write {&cpu_aux_streams_rw[1]};
        std::deque<int> removed_cpus {};
        std::map<int, std::size_t> cpu_fd_counter {};
        std::map<int, async::continuations::stored_continuation_t<>> cpu_shutdown_monitors {};
        std::set<std::shared_ptr<stream_descriptor_t>> primary_streams {};
        std::set<std::shared_ptr<stream_descriptor_t>> supplimentary_streams {};
        async::continuations::stored_continuation_t<> termination_handler {};
        bool live_mode;
        bool busy_polling {false};
        bool poll_all {false};
        bool terminate_complete {false};
        bool terminate_requested {false};
        bool any_added {false};

        /** Asynchronously remove one item from the remove list */
        async::continuations::polymorphic_continuation_t<boost::system::error_code> async_remove()
        {
            using namespace async::continuations;

            LOG_TRACE("called async_remove, t=%u, a=%u, p=%zu, s=%zu, r=%zu",
                      terminate_requested,
                      any_added,
                      primary_streams.size(),
                      supplimentary_streams.size(),
                      removed_cpus.size());

            if (!removed_cpus.empty()) {
                auto const cpu_no = removed_cpus.front();
                removed_cpus.pop_front();

                LOG_TRACE("Requesting to remove ringbuffer for cpu %d", cpu_no);

                return start_on(strand.context()) //
                     | perf_buffer_consumer->async_remove_ringbuffer(cpu_no,
                                                                     use_continuation) //
                     | post_on(strand)                                                 //
                     | then([cpu_no, st = this->shared_from_this()](auto ec) {
                           LOG_TRACE("Removed cpu #%d, got ec=%s", cpu_no, ec.message().c_str());

                           // remove the counter
                           st->cpu_fd_counter.erase(cpu_no);

                           // notify the handler
                           auto handler = std::move(st->cpu_shutdown_monitors[cpu_no]);
                           if (handler) {
                               LOG_TRACE("notifying that mmap %d is removed", cpu_no);
                               resume_continuation(st->strand.context(), std::move(handler));
                           }

                           // remove the next one; any previous error is logged and swallowed
                           return st->async_remove();
                       });
            }

            // have we terminated?
            if (terminate_requested && primary_streams.empty() && supplimentary_streams.empty()
                && removed_cpus.empty()) {
                // yup
                terminate_complete = true;
                // notify the handler
                if (termination_handler) {
                    LOG_TRACE("notifying terminated");
                    resume_continuation(strand.context(), std::move(termination_handler));
                }
            }
            // did all the primary streams close? means the traced app exited
            else if ((!terminate_requested) && any_added && primary_streams.empty() && removed_cpus.empty()) {
                LOG_TRACE("notifying all-exited");
                perf_buffer_consumer->trigger_one_shot_mode();
            }

            return start_with(boost::system::error_code {});
        }

        /** Asynchronously poll either all cpus OR each item in the pending list */
        async::continuations::polymorphic_continuation_t<boost::system::error_code> async_poll(bool poll_all)
        {
            using namespace async::continuations;

            LOG_TRACE("called async_poll, poll_all=%u, t=%u, a=%u, p=%zu, s=%zu, r=%zu",
                      poll_all,
                      terminate_requested,
                      any_added,
                      primary_streams.size(),
                      supplimentary_streams.size(),
                      removed_cpus.size());

            if (poll_all) {
                // clear the per-cpu list as all the cores are about to be polled
                pending_cpus_read->clear();

                LOG_TRACE("Requesting to poll_all");

                return start_on(strand.context())                             //
                     | perf_buffer_consumer->async_poll_all(use_continuation) //
                     | then([](auto ec) {
                           LOG_TRACE("Polled all, got ec=%s", ec.message().c_str());
                           return ec;
                       })              //
                     | map_error()     //
                     | post_on(strand) //
                     | then([st = this->shared_from_this()]() {
                           // move the read list into a local as we want to clear the read list on completion of poll
                           std::map<int, std::set<std::shared_ptr<stream_descriptor_t>>> cpu_aux_streams {
                               std::move(*st->cpu_aux_streams_read)};

                           // re-enable any AUX items that might have got disabled due to mmap full
                           for (auto & entry : cpu_aux_streams) {
                               for (auto & fd : entry.second) {
                                   st->perf_activator->re_enable(fd->native_handle());
                               }
                           }

                           // now remove any queued for remove
                           return st->async_remove();
                       });
            }

            if (!pending_cpus_read->empty()) {
                // poll one item from the pending list
                auto cpu_no = pending_cpus_read->front();
                pending_cpus_read->pop_front();

                LOG_TRACE("Requesting to poll ringbuffer for cpu %d", cpu_no);

                return start_on(strand.context())                                 //
                     | perf_buffer_consumer->async_poll(cpu_no, use_continuation) //
                     | then([cpu_no](auto ec) {
                           LOG_TRACE("Polled cpu #%d, got ec=%s", cpu_no, ec.message().c_str());
                           return ec;
                       })              //
                     | map_error()     //
                     | post_on(strand) //
                     | then([st = this->shared_from_this(), cpu_no]() {
                           // re-enable any AUX items that might have got disabled due to mmap full
                           auto it = st->cpu_aux_streams_read->find(cpu_no);
                           if (it != st->cpu_aux_streams_read->end()) {
                               // re-enable
                               for (auto & fd : it->second) {
                                   st->perf_activator->re_enable(fd->native_handle());
                               }

                               // remove it
                               st->cpu_aux_streams_read->erase(it);
                           }

                           // try again for the next item
                           return st->async_poll(false);
                       });
            }

            // check for any removed items
            return async_remove();
        }

        /** Recursive loop for the body of async_try_poll */
        static async::continuations::polymorphic_continuation_t<boost::system::error_code> async_try_poll_body(
            std::shared_ptr<async_perf_ringbuffer_monitor_t> st)
        {
            using namespace async::continuations;

            // process the list contents
            return st->async_poll(std::exchange(st->poll_all, false)) //
                 | post_on(st->strand)                                //
                 | then([st](auto ec) -> polymorphic_continuation_t<boost::system::error_code> {
                       // swap the read/write pointers again, repeat if there are more events pending...
                       st->swap_read_write_poll_lists();

                       // finish if the new read list is empty
                       if (st->pending_cpus_read->empty()) {
                           LOG_TRACE("async_try_poll :: complete");
                           st->busy_polling = false;
                           return start_with(ec);
                       }

                       LOG_TRACE("async_try_poll :: iterating");

                       // otherwise poll again
                       return async_try_poll_body(st);
                   });
        }

        /** swap the read/write pointers so that the write list becomes the read list and vice-versa. This allows the poll loop to access the read list, whilst the event monitors access the write list */
        void swap_read_write_poll_lists()
        {
            std::swap(pending_cpus_read, pending_cpus_write);
            std::swap(cpu_aux_streams_read, cpu_aux_streams_write);
            runtime_assert(pending_cpus_write->empty(), "expected write list to be empty");
            runtime_assert(cpu_aux_streams_write->empty(), "expected write list to be empty");
        }

        /** Poll if some poll loop was not already active */
        auto async_try_poll()
        {
            using namespace async::continuations;

            LOG_TRACE("async_try_poll");

            auto st = this->shared_from_this();

            return start_on(st->strand)                                                  //
                 | do_if_else([st]() { return !std::exchange(st->busy_polling, true); }, //
                              [st]() {
                                  LOG_TRACE("async_try_poll :: busy");

                                  // swap the read and write list so that the poll loop consumes the old write list, and the monitors can write to the old read list
                                  st->swap_read_write_poll_lists();

                                  return async_try_poll_body(st);
                              },
                              []() {
                                  LOG_TRACE("async_try_poll :: skip");

                                  return boost::system::error_code {};
                              })
                 | map_error();
        }

        /** Observe the file descriptor for read events */
        void spawn_observer_perf_fd(int cpu_no,
                                    std::shared_ptr<stream_descriptor_t> stream_descriptor,
                                    bool primary,
                                    bool is_aux)
        {

            using namespace async::continuations;

            auto st = this->shared_from_this();
            auto nh = stream_descriptor->native_handle();

            LOG_TRACE("Observing new fd %d %d %u", cpu_no, nh, primary);

            // and wait for data to be available
            spawn("perf buffer monitor for event fd",
                  start_on(strand) //
                      | then([st, cpu_no, stream_descriptor, primary]() {
                            if (!st->is_terminate_requested()) {
                                if (primary) {
                                    st->primary_streams.insert(stream_descriptor);
                                    st->cpu_fd_counter[cpu_no] += 1;
                                    st->any_added = true;
                                }
                                else {
                                    st->supplimentary_streams.insert(stream_descriptor);
                                }
                            }
                        }) //
                      | repeatedly(
                          [st]() {
                              return start_on(st->strand) //
                                   | then([st]() { return (!st->is_terminate_requested()); });
                          },
                          [st, nh, cpu_no, stream_descriptor, is_aux]() {
                              LOG_TRACE("waiting for notification on %d / %d", cpu_no, nh);

                              return stream_descriptor->async_wait(boost::asio::posix::stream_descriptor::wait_read,
                                                                   use_continuation)
                                   | post_on(st->strand)
                                   | then([st, nh, cpu_no, stream_descriptor, is_aux](
                                              boost::system::error_code const & ec) -> polymorphic_continuation_t<> {
                                         LOG_TRACE("Received file descriptor notification for cpu=%d, fd=%d, ec=%s",
                                                   cpu_no,
                                                   nh,
                                                   ec.message().c_str());

                                         auto const already_contained =
                                             std::any_of(st->pending_cpus_write->begin(),
                                                         st->pending_cpus_write->end(),
                                                         [cpu_no](int n) { return n == cpu_no; });

                                         // add it to the wait queue, regardless of the error code
                                         if (!already_contained) {
                                             st->pending_cpus_write->emplace_back(cpu_no);
                                         }

                                         // and add the fd to the re-enable set (event if already_contained cpu_no)
                                         if (is_aux) {
                                             (*st->cpu_aux_streams_write)[cpu_no].insert(stream_descriptor);
                                         }

                                         if (ec) {
                                             return start_with(ec) | map_error();
                                         }

                                         if (st->busy_polling || already_contained) {
                                             return {};
                                         }

                                         return st->async_try_poll();
                                     });
                          }),
                  [st, cpu_no, stream_descriptor, primary, is_aux, nh](bool) {
                      // mark it as removed
                      spawn("perf buffer event monitor - final flush",
                            start_on(st->strand) //
                                | then([st, cpu_no, stream_descriptor, primary, is_aux, nh]()
                                           -> polymorphic_continuation_t<> {
                                      LOG_TRACE("Removing file descriptor notification for cpu=%d / %d", cpu_no, nh);

                                      // explicitly close the FD in case we get here for any other reason than EOF
                                      stream_descriptor->close();

                                      if (primary) {
                                          // decrement the per-cpu count
                                          auto const n = --(st->cpu_fd_counter[cpu_no]);

                                          LOG_TRACE("... remove %d -> %zu", nh, n);

                                          if (n == 0) {
                                              // add it to the remove queue
                                              st->removed_cpus.emplace_back(cpu_no);
                                          }

                                          // remove from monitored list
                                          st->primary_streams.erase(stream_descriptor);
                                      }
                                      else {
                                          // remove from monitored list
                                          st->supplimentary_streams.erase(stream_descriptor);
                                      }

                                      if (is_aux) {
                                          // remove it from both lists as it does not need to be re-enabled
                                          auto it_r = st->cpu_aux_streams_read->find(cpu_no);
                                          if (it_r != st->cpu_aux_streams_read->end()) {
                                              it_r->second.erase(stream_descriptor);
                                          }
                                          auto it_w = st->cpu_aux_streams_write->find(cpu_no);
                                          if (it_w != st->cpu_aux_streams_write->end()) {
                                              it_w->second.erase(stream_descriptor);
                                          }
                                      }

                                      if (st->busy_polling) {
                                          return {};
                                      }

                                      return st->async_try_poll();
                                  }));
                  });

            // observe for errors; will be notified when the FD is closed by the kernel on process exit.
            spawn("perf buffer monitor for event fd close handler",
                  stream_descriptor->async_wait(boost::asio::posix::stream_descriptor::wait_error, use_continuation),
                  [st, stream_descriptor, nh](bool f) {
                      // spawn this on the strand so that it's serialized with respect to the reader.
                      spawn("perf buffer monitor stream close",
                            start_on(st->strand) //
                                | then([stream_descriptor, nh, f]() {
                                      LOG_TRACE("Received close notification for %d was %u", nh, f);
                                      if (stream_descriptor->is_open()) {
                                          stream_descriptor->close();
                                      }
                                      else {
                                          LOG_TRACE("Stream descriptor already closed");
                                      }
                                  }));
                  });
        }

        /** Start the timer */
        void do_start_timer()
        {
            using namespace async::continuations;

            auto st = this->shared_from_this();

            spawn("perf buffer timer",
                  repeatedly(
                      [st]() {
                          return start_on(st->strand) //
                               | then([st]() {
                                     return (!st->is_terminate_requested()) || (st->primary_streams.size() > 0);
                                 });
                      }, //
                      [st]() {
                          st->timer.expires_after(st->live_mode ? live_poll_interval : local_poll_interval);

                          return st->timer.async_wait(use_continuation) //
                               | post_on(st->strand)                    //
                               | then([st](boost::system::error_code const & ec) -> polymorphic_continuation_t<> {
                                     LOG_TRACE("Timer tick: %s", ec.message().c_str());

                                     // swallow cancel errors as it's just the timer being woken early
                                     if ((ec)
                                         && (ec
                                             != boost::asio::error::make_error_code(
                                                 boost::asio::error::operation_aborted))) {
                                         return start_with(ec) | map_error();
                                     }

                                     // if no error, then timeout occured so trigger a poll_all
                                     if (!ec) {
                                         st->poll_all = true;
                                     }

                                     if (st->busy_polling) {
                                         return {};
                                     }

                                     return st->async_try_poll();
                                 });
                      }),
                  [st](bool) {
                      // always perform a final flush of any remaining data
                      spawn("perf buffer event timer - final flush",
                            start_on(st->strand)                        //
                                | then([st]() { st->poll_all = true; }) //
                                | st->async_try_poll());
                  });
        }
    };
}
