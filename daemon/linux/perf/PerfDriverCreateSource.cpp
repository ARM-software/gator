/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "Command.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "SessionData.h"
#include "linux/perf/PerfDriver.h"
#include "linux/perf/PerfSource.h"

/// this method is extracted so that it can be excluded from the unit tests as it brings deps on PerfSource...

std::unique_ptr<PerfSource> PerfDriver::create_source(sem_t & senderSem,
                                                      std::function<void()> profilingStartedCallback,
                                                      std::set<int> appTids,
                                                      FtraceDriver & ftraceDriver,
                                                      bool enableOnCommandExec,
                                                      ICpuInfo & cpuInfo)
{
    auto attrs_buffer = std::make_unique<PerfAttrsBuffer>(gSessionData.mTotalBufferSize * MEGABYTES, senderSem);

    perf_event_group_configurer_config_t event_configurer_config {
        mConfig.config,
        cpuInfo.getClusters(),
        cpuInfo.getClusterIds(),
        mConfig.config.exclude_kernel || gSessionData.mExcludeKernelEvents,
        PerfSource::createPerfBufferConfig(),
        getTracepointId(traceFsConstants, SCHED_SWITCH),
        // We disable periodic sampling if we have at least one EBS counter
        // it should probably be independent of EBS though
        gSessionData.mBacktraceDepth,
        gSessionData.mSampleRate,
        !gSessionData.mIsEBS,
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
        perf_groups_configurer_t groups_builder {event_configurer_config, event_configurer_state};
        attr_to_key_mapping_tracker_t wrapper {*attrs_buffer};
        if (!enable(groups_builder, wrapper)) {
            LOG_DEBUG("perf setup failed, are you running Linux 3.4 or later?");
            return {};
        }
        // TODO: async send mapping tracked items
    }

    attrs_buffer->flush();

    auto result = std::make_unique<PerfSource>(
        event_configurer_config,
        perf_groups_activator_state_t::convert_from(std::move(event_configurer_state)),
        std::move(attrs_buffer),
        senderSem,
        std::move(profilingStartedCallback),
        [this](auto & a, auto b) { return summary(a, b); },
        [this](auto & a, auto b) { return coreName(a, b); },
        [this](auto & a, auto b) { return read(a, b); },
        std::move(appTids),
        ftraceDriver,
        enableOnCommandExec,
        cpuInfo);

    if (!result->prepare()) {
        return {};
    }

    return result;
}
