/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#pragma once

// Define to adjust Buffer.h interface,
#define BUFFER_USE_SESSION_DATA
// must be before includes

#include "Buffer.h"
#include "ISender.h"
#include "Source.h"
#include "agents/perf/perf_agent_worker.h"
#include "ipc/messages.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include <semaphore.h>

namespace agents::perf {

    class perf_source_adapter_t : public PrimarySource {
    public:
        explicit perf_source_adapter_t(sem_t & sender_sem,
                                       ISender & sender,
                                       std::function<void(bool, std::vector<pid_t>)> agent_started_callback,
                                       std::function<void()> exec_target_app_callback,
                                       std::function<void()> profiling_started_callback);

        ~perf_source_adapter_t() override = default;

        /**
         * Note: this method doesn't actually send the summary frame as that is done by the new
         * perf agent at the start of capture. This method is required by the legacy code as it is
         * the point at which the monotonic start time is established for all sources.
         */
        std::optional<std::uint64_t> sendSummary() override;

        /** The main blocking body of the source which runs and waits for the capture to complete */
        void run(std::uint64_t monotonicStart, std::function<void()> endSession) override;

        /**
         * Called by Child to stop the capture from the "shell" side. We need to ask the agent
         * to shut down.
         */
        void interrupt() override;

        /** @return True when capture ended */
        bool write(ISender & sender) override;

        /**
         * Called by the agent worker to set itself as a controller for this adapter.
         *
         * CALLED FROM THE ASIO THREAD POOL
         */
        void set_controller(std::unique_ptr<perf_capture_controller_t> controller);

        /**
         * Called by the agent worker once the agent ready message has been received.
         *
         * CALLED FROM THE ASIO THREAD POOL
         * @param monitored_pids A list of PIDs being monitored by the worker,
         * only the primary source (i.e. the perf agent) will provide these
         */
        void on_capture_ready(std::vector<pid_t> monitored_pids = {});

        /**
         * Called by the agent worker once the start message has been sent successfully.
         *
         * CALLED FROM THE ASIO THREAD POOL
         */
        void on_capture_started();

        /**
         * Called by the agent worker when the shutdown message is received. If the shutdown was initiated
         * by the agent then the endSession callback needs to be invoked so that the Child process can
         * terminate any other sources.
         *
         * CALLED FROM THE ASIO THREAD POOL
         */
        void on_capture_completed();

        /**
         * Called by the worker to deliver any APC frames that get sent by the agent.
         *
         * CALLED FROM THE ASIO THREAD POOL
         */
        void on_apc_frame_received(const std::vector<char> & frame);

        /**
         * Called by the worker when the capture fails
         *
         * CALLED FROM THE ASIO THREAD POOL
         *
         * @param reason The failure reason
         */
        void on_capture_failed(ipc::capture_failed_reason_t reason);

        /**
         * Called by the worker to trigger the launch of some android apk
         *
         * CALLED FROM THE ASIO THREAD POOL
         */
        void exec_target_app();

    private:
        sem_t & sender_sem;
        ISender & sender;

        // variables that are guarded by the event_mutex
        std::mutex event_mutex;
        std::function<void(bool, std::vector<pid_t>)> agent_started_callback;
        std::function<void()> exec_target_app_callback;
        std::function<void()> profiling_started_callback;
        std::unique_ptr<perf_capture_controller_t> capture_controller;
        bool shutdown_initiated_from_shell;
        std::function<void()> end_session;

        // capture_ended is an atomic var rather than being guarded by the event mutex since this
        // ends up getting checked frequently when the write buffer is flushed. doing it this way
        // we can avoid the overhead of stronger memory ordering imposed by the mutex.
        std::atomic_bool capture_ended;
    };
}
