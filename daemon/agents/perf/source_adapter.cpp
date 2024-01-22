/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#include "agents/perf/source_adapter.h"

#include "ISender.h"
#include "Logging.h"
#include "Time.h"
#include "agents/perf/perf_agent_worker.h"
#include "ipc/messages.h"
#include "lib/Assert.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/use_future.hpp>

#include <semaphore.h>
#include <sys/types.h>

namespace agents::perf {
    perf_source_adapter_t::perf_source_adapter_t(sem_t & sender_sem,
                                                 ISender & sender,
                                                 std::function<void(bool, std::vector<pid_t>)> agent_started_callback,
                                                 std::function<void()> exec_target_app_callback,
                                                 std::function<void()> profiling_started_callback)
        : sender_sem(sender_sem),
          sender(sender),
          agent_started_callback(std::move(agent_started_callback)),
          exec_target_app_callback(std::move(exec_target_app_callback)),
          profiling_started_callback(std::move(profiling_started_callback)),
          capture_ended(false)
    {
    }

    std::optional<std::uint64_t> perf_source_adapter_t::sendSummary()
    {
        return {getTime()};
    }

    void perf_source_adapter_t::run(std::uint64_t monotonicStart, std::function<void()> endSession)
    {
        {
            auto lock = std::unique_lock(event_mutex);
            end_session = std::move(endSession);
            shutdown_initiated_from_shell = false;
        }

        // ask the agent to start capturing
        auto started_success = capture_controller->async_start_capture(monotonicStart, boost::asio::use_future);

        // release the lock while we wait for this to happen
        if (!started_success.get()) {
            LOG_ERROR("Perf agent failed to start capture");
            handleException();
        }
    }

    void perf_source_adapter_t::interrupt()
    {
        perf_capture_controller_t * capture_controller = nullptr;

        {
            auto lock = std::unique_lock(event_mutex);
            shutdown_initiated_from_shell = true;
            capture_controller = this->capture_controller.get();
        }

        if (capture_controller != nullptr) {
            auto f = capture_controller->async_stop_capture(boost::asio::use_future);
            f.get();
        }
    }

    bool perf_source_adapter_t::write(ISender & /*sender*/)
    {
        return capture_ended.load(std::memory_order_relaxed);
    }

    void perf_source_adapter_t::set_controller(std::unique_ptr<perf_capture_controller_t> controller)
    {
        auto lock = std::unique_lock(event_mutex);
        capture_controller = std::move(controller);
    }

    void perf_source_adapter_t::on_capture_ready(std::vector<pid_t> monitored_pids)
    {
        std::function<void(bool, std::vector<pid_t>)> f;
        {
            auto lock = std::unique_lock(event_mutex);
            f = std::exchange(agent_started_callback, std::function<void(bool, std::vector<pid_t>)>());
        }
        if (f) {
            f(true, std::move(monitored_pids));
        }
    }

    void perf_source_adapter_t::on_capture_started()
    {
        std::function<void()> f;
        {
            auto lock = std::unique_lock(event_mutex);
            f = std::exchange(profiling_started_callback, std::function<void()>());

            runtime_assert(!agent_started_callback, "on_capture_ready was not called");
        }
        if (f) {
            f();
        }
    }

    void perf_source_adapter_t::on_capture_completed()
    {
        capture_ended.store(true, std::memory_order_relaxed);

        std::function<void(bool, std::vector<pid_t>)> local_agent_started;
        std::function<void()> local_end_session;

        {
            auto lock = std::unique_lock(event_mutex);
            local_agent_started =
                std::exchange(agent_started_callback, std::function<void(bool, std::vector<pid_t>)>());
            if (!shutdown_initiated_from_shell) {
                local_end_session = std::exchange(end_session, std::function<void()>());
            }
        }

        if (local_agent_started) {
            local_agent_started(false, std::vector<pid_t> {});
        }

        if (local_end_session) {
            local_end_session();
        }
        else {
            sem_post(&sender_sem);
        }
    }

    void perf_source_adapter_t::on_apc_frame_received(const std::vector<char> & frame)
    {
        auto const length = frame.size();

        runtime_assert(length <= ISender::MAX_RESPONSE_LENGTH, "too large apc_frame msg received");

        sender.writeData(frame.data(), static_cast<int>(length), ResponseType::APC_DATA);
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void perf_source_adapter_t::on_capture_failed(ipc::capture_failed_reason_t reason)
    {
        // NOLINTNEXTLINE(hicpp-multiway-paths-covered)
        switch (reason) {
            case ipc::capture_failed_reason_t::command_exec_failed: {
                LOG_ERROR("Capture failed due to exec failure");
                handleException();
                break;
            }
            default: {
                LOG_ERROR("Unexpected capture failure reason");
                handleException();
                break;
            }
        }
    }

    void perf_source_adapter_t::exec_target_app()
    {
        exec_target_app_callback();
    }
}
