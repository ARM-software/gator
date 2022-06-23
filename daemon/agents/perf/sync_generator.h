/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "BufferUtils.h"
#include "IRawFrameBuilder.h"
#include "ISender.h"
#include "Protocol.h"
#include "agents/perf/async_buffer_builder.h"
#include "ipc/messages.h"
#include "ipc/raw_ipc_channel_sink.h"
#include "linux/perf/PerfSyncThread.h"

#include <cstdint>

namespace agents::perf {
    /**
     * Collects the timing data from the owned sync thread, encodes into an
     * APC frame, and sends it via the IPC sink.
     *
     * @tparam SyncThread Thread-owning type that produces the timing data
     * and peforms the thread renaming
     */
    template<typename SyncThread>
    class basic_sync_generator_t {
    public:
        /**
          * Factory method, creates appropriate number of sync thread objects
         *
         * @param supports_clock_id True if the kernel perf API supports configuring clock_id
         * @param has_spe_configuration True if the user selected at least one SPE configuration
         * @param sink IPC channel to write the resulting APC frame into
         * @return sync_generator instance, or nullptr if supports_clock_id and !has_spe_configuration
         */
        static std::unique_ptr<basic_sync_generator_t> create(bool supports_clock_id,
                                                              bool has_spe_configuration,
                                                              std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink)
        {
            if (has_spe_configuration || !supports_clock_id) {
                const bool enable_sync_thread_mode = (!supports_clock_id);
                const bool read_timer = has_spe_configuration;
                return std::make_unique<basic_sync_generator_t>(enable_sync_thread_mode, read_timer, std::move(sink));
            }

            return nullptr;
        }

        /**
         * Constructor
         *
         * @param enable_sync_thread_mode True to enable 'gatord-sync' thread mode
         * @param read_timer True to read the arch timer, false otherwise
         * @param sink IPC channel to write the resulting APC frame into
         */
        basic_sync_generator_t(bool enable_sync_thread_mode,
                               bool read_timer,
                               std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink)
            : sink {std::move(sink)}, thread {enable_sync_thread_mode, read_timer, [this](auto... args) {
                                                  write(args...);
                                              }}
        {
        }

        /**
         * Start thread
         *
         * @param monotonic_raw_base The monotonic raw value that equates to monotonic delta 0
         */
        void start(std::uint64_t monotonic_raw_base) { thread.start(monotonic_raw_base); }

        /**
         * Stop and join thread
         */
        void terminate() { thread.terminate(); }

    private:
        static constexpr std::size_t max_sync_buffer_size = IRawFrameBuilder::MAX_FRAME_HEADER_SIZE // Header
                                                          + buffer_utils::MAXSIZE_PACK32            // Length
                                                          + buffer_utils::MAXSIZE_PACK32            // CPU (ignored)
                                                          + buffer_utils::MAXSIZE_PACK32            // pid
                                                          + buffer_utils::MAXSIZE_PACK32            // tid
                                                          + buffer_utils::MAXSIZE_PACK64            // freq
                                                          + buffer_utils::MAXSIZE_PACK64            // monotonic_raw
                                                          + buffer_utils::MAXSIZE_PACK64;           // vcnt

        std::shared_ptr<ipc::raw_ipc_channel_sink_t> sink;
        std::vector<char> buffer;
        SyncThread thread;

        void write(pid_t pid, pid_t tid, std::uint64_t freq, std::uint64_t monotonic_raw, std::uint64_t vcnt)
        {
            buffer.resize(max_sync_buffer_size);
            auto builder = apc_buffer_builder_t(buffer);

            // Begin frame, the size field and other header data will be added
            // by the receiver
            builder.beginFrame(FrameType::PERF_SYNC);
            builder.packInt(0); // just pass CPU == 0, Since Streamline 7.4 it is ignored anyway

            // Write header
            builder.packInt(pid);
            builder.packInt(tid);
            builder.packInt64(static_cast<std::int64_t>(freq));

            // Write record
            builder.packInt64(static_cast<std::int64_t>(monotonic_raw));
            builder.packInt64(static_cast<std::int64_t>(vcnt));

            builder.endFrame();

            LOG_DEBUG("Committing perf sync data (freq: %" PRIu64 ", monotonic: %" PRIu64 ", vcnt: %" PRIu64
                      ") written: %zu bytes",
                      freq,
                      monotonic_raw,
                      vcnt,
                      builder.getWriteIndex());

            // Send frame
            sink->async_send_message(ipc::msg_apc_frame_data_t {std::move(buffer)},
                                     [](auto const & ec, auto const & /*msg*/) {
                                         // EOF means terminated
                                         if (ec && ec != boost::asio::error::eof) {
                                             LOG_DEBUG("Failed to send IPC message due to %s", ec.message().c_str());
                                         }
                                     });
        }
    };

    /**
     * Helper alias for the standard sync thread type.
     */
    using sync_generator = basic_sync_generator_t<PerfSyncThread>;
}
