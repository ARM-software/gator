/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "Child.h"
#include "DynBuf.h"
#include "ICpuInfo.h"
#include "ISender.h"
#include "Logging.h"
#include "Proc.h"
#include "SessionData.h"
#include "Source.h"
#include "agents/agent_workers_process.h"
#include "agents/perf/capture_configuration.h"
#include "ipc/messages.h"
#include "lib/Utils.h"
#include "linux/perf/PerfAttrsBuffer.h"
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfDriverConfiguration.h"
#include "linux/perf/PerfGroups.h"
#include "xml/PmuXML.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <boost/asio/use_future.hpp>

namespace {
    constexpr std::size_t MEGABYTES = 1024UL * 1024UL;
    constexpr std::size_t AUX_MULTIPLIER = 64UL; // size multiplier for session buffer size to aux buffer size

    // this used to be done in PerfSource::run but is not part of the new perf agent.
    // it's placed here as a stop-gap measure until the ftrace agent has been written.
    void send_tracepoint_formats(FtraceDriver & driver, PerfAttrsBuffer & buffer, bool system_wide)
    {
        DynBuf printb;
        DynBuf b1;

        if (!readProcSysDependencies(buffer, &printb, &b1, driver)) {
            if (system_wide) {
                LOG_ERROR("readProcSysDependencies failed");
                handleException();
            }
            else {
                LOG_FINE("readProcSysDependencies failed");
            }
        }

        buffer.flush();
    }

    std::map<std::uint32_t, std::string> collect_pmu_type_to_name_map(PerfDriverConfiguration & config)
    {
        std::map<std::uint32_t, std::string> type_to_name;

        for (const auto & uncore : config.uncores) {
            type_to_name[static_cast<std::uint32_t>(uncore.pmu_type)] = uncore.uncore_pmu.getId();
        }

        return type_to_name;
    }

    agents::perf::buffer_config_t create_perf_buffer_config()
    {
        return {
            static_cast<size_t>(gSessionData.mPageSize),
            (gSessionData.mPerfMmapSizeInPages > 0
                 ? static_cast<size_t>(gSessionData.mPageSize * gSessionData.mPerfMmapSizeInPages)
                 : static_cast<size_t>(gSessionData.mTotalBufferSize) * MEGABYTES),
            (gSessionData.mPerfMmapSizeInPages > 0
                 ? static_cast<size_t>(gSessionData.mPageSize * gSessionData.mPerfMmapSizeInPages)
                 : static_cast<size_t>(gSessionData.mTotalBufferSize) * MEGABYTES * AUX_MULTIPLIER),
        };
    }

}

/// this method is extracted so that it can be excluded from the unit tests as it brings deps on PerfSource...

std::shared_ptr<PrimarySource> PerfDriver::create_source(sem_t & senderSem,
                                                         ISender & sender,
                                                         std::function<bool()> session_ended_callback,
                                                         std::function<void()> exec_target_app_callback,
                                                         std::function<void()> profilingStartedCallback,
                                                         const std::set<int> & appTids,
                                                         FtraceDriver & ftraceDriver,
                                                         bool enableOnCommandExec,
                                                         ICpuInfo & cpuInfo,
                                                         lib::Span<UncorePmu> uncore_pmus,
                                                         agents::agent_workers_process_t<Child> & agent_workers_process)
{
    auto attrs_buffer = std::make_unique<PerfAttrsBuffer>(gSessionData.mTotalBufferSize * MEGABYTES, senderSem);

    perf_event_group_configurer_config_t event_configurer_config {
        mConfig.config,
        cpuInfo.getClusters(),
        cpuInfo.getClusterIds(),
        mConfig.config.exclude_kernel || gSessionData.mExcludeKernelEvents,
        create_perf_buffer_config(),
        getTracepointId(traceFsConstants, SCHED_SWITCH),
        // We disable periodic sampling if we have at least one EBS counter
        // it should probably be independent of EBS though
        gSessionData.mBacktraceDepth,
        gSessionData.mSampleRate,
        !gSessionData.mIsEBS,
        gSessionData.mEnableOffCpuSampling,
    };

    perf_groups_configurer_state_t event_configurer_state {};

    // Reread cpuinfo since cores may have changed since startup
    cpuInfo.updateIds(false);

    // write out any tracepoint format descriptors
    if (mConfig.config.can_access_tracepoints && !sendTracepointFormats(*attrs_buffer)) {
        LOG_DEBUG("could not send tracepoint formats");
        return {};
    }

    {
        attr_to_key_mapping_tracker_t wrapper {*attrs_buffer};
        perf_groups_configurer_t groups_builder {wrapper, event_configurer_config, event_configurer_state};
        if (!enable(groups_builder, wrapper)) {
            LOG_WARNING("perf setup failed, are you running Linux 3.4 or later?");
            return {};
        }
    }

    // write directly to the sender
    attrs_buffer->flush();
    attrs_buffer->write(sender);

    // add the tracepoint formats
    send_tracepoint_formats(ftraceDriver, *attrs_buffer, mConfig.config.is_system_wide);
    // write directly to the sender
    attrs_buffer->flush();
    attrs_buffer->write(sender);

    return create_source_adapter(agent_workers_process,
                                 senderSem,
                                 sender,
                                 std::move(session_ended_callback),
                                 std::move(exec_target_app_callback),
                                 std::move(profilingStartedCallback),
                                 appTids,
                                 uncore_pmus,
                                 event_configurer_state,
                                 create_perf_buffer_config(),
                                 enableOnCommandExec);
}

[[nodiscard]] static bool wait_for_ready(std::optional<bool> const & ready_worker,
                                         std::optional<bool> const & ready_agent,
                                         bool session_ended)
{
    // wait, if no worker value received
    if (!ready_worker) {
        return true;
    }

    // stop waiting if worker failed
    if (!*ready_worker) {
        return false;
    }

    // stop if the session ended
    if (session_ended) {
        return false;
    }

    // worker is started successfully, wait for agent
    return (!ready_agent);
}

std::shared_ptr<agents::perf::perf_source_adapter_t> PerfDriver::create_source_adapter(
    agents::agent_workers_process_t<Child> & agent_workers_process,
    sem_t & senderSem,
    ISender & sender,
    std::function<bool()> session_ended_callback, // NOLINT(performance-unnecessary-value-param)
    std::function<void()> exec_target_app_callback,
    std::function<void()> profiling_started_callback,
    const std::set<int> & app_tids,
    lib::Span<UncorePmu> uncore_pmus,
    const perf_groups_configurer_state_t & perf_groups,
    const agents::perf::buffer_config_t & ringbuffer_config,
    bool enable_on_exec)
{
    auto cluster_keys_for_freq_counter = get_cpu_cluster_keys_for_cpu_frequency_counter();

    std::vector<GatorCpu> gator_cpus;
    for (const auto & cpu : mConfig.cpus) {
        gator_cpus.push_back(cpu.gator_cpu);
    }

    std::map<int, std::uint32_t> cpu_to_spe_type;
    for (auto & e : mConfig.cpuNumberToSpeType) {
        cpu_to_spe_type[e.first] = static_cast<std::uint32_t>(e.second);
    }

    auto type_to_name_map = collect_pmu_type_to_name_map(mConfig);

    ipc::msg_capture_configuration_t config_msg = agents::perf::create_capture_configuration_msg(
        gSessionData,
        mConfig.config,
        mCpuInfo,
        cpu_to_spe_type,
        cluster_keys_for_freq_counter,
        uncore_pmus,
        mPmuXml.cpus,
        perf_groups,
        ringbuffer_config,
        type_to_name_map,
        enable_on_exec,
        // only use SIGSTOP pause when waiting for newly launched Android package
        (gSessionData.mAndroidPackage != nullptr));

    {
        auto uid_gid = lib::resolve_uid_gid(gSessionData.mCaptureUser);
        if (uid_gid && (!gSessionData.mCaptureCommand.empty())) {
            agents::perf::add_command(config_msg,
                                      gSessionData.mCaptureCommand,
                                      gSessionData.mCaptureWorkingDir,
                                      uid_gid->first,
                                      uid_gid->second);
        }
    }
    agents::perf::add_pids(config_msg, app_tids);
    agents::perf::add_wait_for_process(config_msg, gSessionData.mWaitForProcessCommand);
    if (gSessionData.mAndroidPackage != nullptr) {
        agents::perf::add_android_package(config_msg, gSessionData.mAndroidPackage);
    }

    // start the agent worker and tell it to communicate with the source adapter
    struct wait_state_t {
        std::mutex ready_mutex {};
        std::condition_variable condition {};
        std::optional<bool> ready_worker {};
        std::optional<bool> ready_agent {};
    };

    auto wait_state = std::make_shared<wait_state_t>();

    auto source = std::make_shared<agents::perf::perf_source_adapter_t>(
        senderSem,
        sender,
        [wait_state, &agent_workers_process](bool success, std::vector<pid_t> monitored_pids) {
            LOG_DEBUG("Received agent-ready notification, success=%u", success);
            {
                auto lock = std::unique_lock(wait_state->ready_mutex);
                wait_state->ready_agent = success;
            }
            wait_state->condition.notify_one();

            if (success) {
                agent_workers_process.async_broadcast_when_ready(ipc::msg_monitored_pids_t {std::move(monitored_pids)},
                                                                 async::continuations::use_continuation) //
                    | DETACH_LOG_ERROR("Monitored PID broadcast");
            }
        },
        std::move(exec_target_app_callback),
        std::move(profiling_started_callback));

    agent_workers_process.async_add_perf_source(source, std::move(config_msg), [wait_state](bool success) {
        LOG_DEBUG("Received worker-ready notification, success=%u", success);
        {
            auto lock = std::unique_lock(wait_state->ready_mutex);
            wait_state->ready_worker = success;
        }
        wait_state->condition.notify_one();
    });

    LOG_DEBUG("Waiting for perf agent worker and agent to start");

    {
        constexpr std::chrono::milliseconds poll_session_ended_timeout {100};

        auto lock = std::unique_lock(wait_state->ready_mutex);
        while (wait_for_ready(wait_state->ready_worker, wait_state->ready_agent, session_ended_callback())) {
            wait_state->condition.wait_for(lock, poll_session_ended_timeout);
        }

        if ((!wait_state->ready_worker) || (!*(wait_state->ready_worker))) {
            LOG_ERROR("Failed to start perf agent worker");
            handleException();
        }

        if (session_ended_callback()) {
            // this is not an error; just Child.cpp will shutdown properly
            LOG_DEBUG("Perf agent worker started, but agent not ready by time session ended");
            return source;
        }

        if ((!wait_state->ready_agent) || (!*(wait_state->ready_agent))) {
            LOG_ERROR("Failed to start perf agent");
            handleException();
        }
    }

    LOG_DEBUG("Perf agent worker started");
    return source;
}
