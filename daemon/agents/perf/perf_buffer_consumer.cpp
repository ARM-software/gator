/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/perf_buffer_consumer.h"

#include "BufferUtils.h"
#include "ISender.h"
#include "agents/perf/async_buffer_builder.h"
#include "agents/perf/perf_frame_packer.hpp"
#include "async/continuations/continuation.h"
#include "async/continuations/stored_continuation.h"
#include "ipc/messages.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/error_code_or.hpp"

#include <boost/system/error_code.hpp>

namespace agents::perf {
    namespace {
        /** Read the aux_head/data_head field from the header */
        template<__u64 perf_event_mmap_page::*Field>
        [[nodiscard]] static constexpr __u64 atomic_load_field(perf_event_mmap_page * header)
        {
            return __atomic_load_n(&(header->*Field), __ATOMIC_ACQUIRE);
        }

        /** Write the aux_tail/data_tail field to the header */
        template<__u64 perf_event_mmap_page::*Field>
        static constexpr void atomic_store_field(perf_event_mmap_page * header, __u64 value)
        {
            __atomic_store_n(&(header->*Field), value, __ATOMIC_RELEASE);
        }
    }

    async::continuations::polymorphic_continuation_t<std::uint64_t, std::uint64_t, boost::system::error_code>
    perf_buffer_consumer_t::do_send_msg(std::shared_ptr<perf_buffer_consumer_t> const & st,
                                        int cpu,
                                        std::vector<char> buffer,
                                        std::uint64_t head,
                                        std::uint64_t tail)
    {
        using namespace async::continuations;

        LOG_TRACE("Sending IPC message for cpu=%d , head=%" PRIu64 " , tail=%" PRIu64 " , size=%zu",
                  cpu,
                  head,
                  tail,
                  buffer.size());

        // update the running total (for one-shot mode)
        st->cumulative_bytes_sent_apc_frames.fetch_add(buffer.size(), std::memory_order_acq_rel);

        // send one-shot notification?
        if (st->is_one_shot_full()) {
            stored_continuation_t<> sc {std::move(st->one_shot_mode_observer)};
            if (sc) {
                resume_continuation(st->strand.context(), std::move(sc));
            }
        }

        runtime_assert(buffer.size() <= ISender::MAX_RESPONSE_LENGTH, "Too large APC frame created");

        // send the message
        return st->ipc_sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(buffer)}, use_continuation) //
             | then([head, tail](auto ec, auto /*msg*/) {
                   LOG_TRACE("... sent, ec=%s , head=%" PRIu64 " , tail=%" PRIu64, ec.message().c_str(), head, tail);

                   return std::make_tuple(head, tail, ec);
               })
             | unpack_tuple();
    }

    template<__u64 perf_event_mmap_page::*HeadField, __u64 perf_event_mmap_page::*TailField, typename Op>
    async::continuations::polymorphic_continuation_t<boost::system::error_code, bool>
    perf_buffer_consumer_t::do_send_common(std::shared_ptr<perf_buffer_consumer_t> const & st,
                                           std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
                                           int cpu,
                                           Op && op)
    {
        using namespace async::continuations;

        auto * header = mmap->header();

        std::uint64_t const head = atomic_load_field<HeadField>(header);
        std::uint64_t const tail = (header->*TailField);

        LOG_TRACE("... cpu=%d , head=%" PRIu64 " , tail=%" PRIu64, cpu, head, tail);

        // no data, no error
        if (head <= tail) {
            return start_with(boost::system::error_code {}, false);
        }

        // is the one-shot mode limit met, if so just skip the data
        if (st->is_one_shot_full()) {
            LOG_TRACE("... skipping (one-shot), cpu=%d , head=%" PRIu64 " , tail=%" PRIu64, cpu, head, tail);
            atomic_store_field<TailField>(mmap->header(), head);

            return start_with(boost::system::error_code {}, false);
        }

        // iterate the data and send it
        return start_with(head, tail, boost::system::error_code {}) //
             | loop([](std::uint64_t head,
                       std::uint64_t tail,
                       boost::system::error_code ec) { return start_with((head > tail) && !ec, head, tail, ec); },
                    [op = std::forward<Op>(op), st, mmap](std::uint64_t head,
                                                          std::uint64_t tail,
                                                          boost::system::error_code ec) {
                        return op(head, tail, ec)            //
                             | post_on(st->strand.context()) //
                             | then([mmap](std::uint64_t h, std::uint64_t t, boost::system::error_code c) {
                                   atomic_store_field<TailField>(mmap->header(), std::min(h, t));
                                   return start_with(h, t, c);
                               });
                    })
             | then([cpu, mmap](std::uint64_t head, std::uint64_t tail, boost::system::error_code ec) {
                   LOG_TRACE("... completed, cpu=%d , head=%" PRIu64 " , tail=%" PRIu64, cpu, head, tail);
                   atomic_store_field<TailField>(mmap->header(), std::min(head, tail));
                   return start_with(ec, true);
               });
    }

    async::continuations::polymorphic_continuation_t<boost::system::error_code, bool>
    perf_buffer_consumer_t::do_send_aux_section(std::shared_ptr<perf_buffer_consumer_t> const & st,
                                                std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
                                                int cpu,
                                                boost::system::error_code ec_from_data,
                                                bool modified_from_data)
    {
        using namespace async::continuations;

        // just forward the error
        if (ec_from_data) {
            LOG_TRACE("Sending data for %d gave error %s", cpu, ec_from_data.message().c_str());
            return start_with(ec_from_data, modified_from_data);
        }

        if (!mmap->has_aux()) {
            return start_with(boost::system::error_code {}, modified_from_data);
        }

        LOG_TRACE("Sending aux data for %d", cpu);

        return do_send_common<&perf_event_mmap_page::aux_head, &perf_event_mmap_page::aux_tail>(
            st,
            mmap,
            cpu,
            [st, mmap, cpu](std::uint64_t const header_head,
                            std::uint64_t const header_tail,
                            boost::system::error_code ec)
                -> polymorphic_continuation_t<std::uint64_t, std::uint64_t, boost::system::error_code> {
                //
                LOG_TRACE("Sending aux chunk for cpu=%d , head=%" PRIu64 " , tail=%" PRIu64,
                          cpu,
                          header_head,
                          header_tail);

                auto const aux_buffer = mmap->aux_span();

                if (header_head <= header_tail) {
                    return start_with(header_head, header_head, ec);
                }

                // find the data to send
                auto [first_span, second_span] =
                    extract_one_perf_aux_apc_frame_data_span_pair(aux_buffer, header_head, header_tail);

                // encode the message
                auto [new_tail, buffer] = encode_one_perf_aux_apc_frame(cpu, first_span, second_span, header_tail);

                runtime_assert(!buffer.empty(), "Expected some apc frame data");

                // send it
                return do_send_msg(st, cpu, std::move(buffer), header_head, new_tail);
            });
    }

    async::continuations::polymorphic_continuation_t<boost::system::error_code, bool>
    perf_buffer_consumer_t::do_send_data_section(std::shared_ptr<perf_buffer_consumer_t> const & st,
                                                 std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
                                                 int cpu)
    {

        using namespace async::continuations;

        LOG_TRACE("Sending data for %d", cpu);

        return do_send_common<&perf_event_mmap_page::data_head, &perf_event_mmap_page::data_tail>(
            st,
            mmap,
            cpu,
            [st, mmap, cpu](std::uint64_t const header_head,
                            std::uint64_t const header_tail,
                            boost::system::error_code ec)
                -> polymorphic_continuation_t<std::uint64_t, std::uint64_t, boost::system::error_code> {
                LOG_TRACE("Sending data chunk for cpu=%d , head=%" PRIu64 " , tail=%" PRIu64,
                          cpu,
                          header_head,
                          header_tail);

                if (header_head <= header_tail) {
                    return start_with(header_head, header_head, ec);
                }

                // encode the data into an apc frame
                auto [new_tail, buffer] =
                    extract_one_perf_data_apc_frame(cpu, mmap->data_span(), header_head, header_tail);

                runtime_assert(!buffer.empty(), "Expected some apc frame data");

                // send it
                return do_send_msg(st, cpu, std::move(buffer), header_head, new_tail);
            });
    }

    [[nodiscard]] async::continuations::polymorphic_continuation_t<boost::system::error_code>
    perf_buffer_consumer_t::do_poll(std::shared_ptr<perf_buffer_consumer_t> const & st,
                                    std::shared_ptr<perf_ringbuffer_mmap_t> const & mmap,
                                    int cpu)
    {
        using namespace async::continuations;

        // SDDAP-11384, read data before aux

        return do_send_data_section(st, mmap, cpu) //
             | then([st, mmap, cpu](boost::system::error_code const & ec, bool modified) {
                   return do_send_aux_section(st, mmap, cpu, ec, modified);
               })                  //
             | post_on(st->strand) //
             | then([st, mmap, cpu](boost::system::error_code const & ec,
                                    bool modified) mutable -> polymorphic_continuation_t<boost::system::error_code> {
                   // not removed / error path
                   if ((ec) || (st->removed_cpus.count(cpu) <= 0)) {
                       // mark it as no longer busy
                       st->busy_cpus.erase(cpu);
                       return start_with(ec);
                   }

                   LOG_TRACE("Remove mmap flush for %d", cpu);

                   // when removed, do it again repeatedly to flush any remainig data since the remove request (which may overlap the sending)
                   return start_with(boost::system::error_code {}, modified)
                        | loop(
                              [](boost::system::error_code const & ec, bool modified) {
                                  LOG_TRACE("Remove send loop will iterate (modified=%u, ec=%s)",
                                            modified,
                                            ec.message().c_str());
                                  // only continue to iterate if no error and last iteration indicates modified ringbuffer data
                                  return start_with(modified && !ec, ec, modified);
                              },
                              [st, mmap, cpu](boost::system::error_code const & /*ec*/, bool /*modified*/) {
                                  return do_send_data_section(st, mmap, cpu) //
                                       | then([st, mmap, cpu](boost::system::error_code e, bool m) {
                                             return do_send_aux_section(st, mmap, cpu, e, m);
                                         });
                              })
                        | post_on(st->strand) //
                        | then([st, cpu](boost::system::error_code const & ec, bool /*modified*/) {
                              LOG_TRACE("Remove mmap completed for %d (poll ec =%s)", cpu, ec.message().c_str());
                              // mark it as no longer busy
                              st->busy_cpus.erase(cpu);
                              // remove it
                              st->per_cpu_mmaps.erase(cpu);
                              return ec;
                          });
               });
    }
}
