/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */

#include "MaliHwCntrTask.h"

#include "GetEventKey.h"
#include "IBlockCounterFrameBuilder.h"
#include "IBufferControl.h"
#include "Logging.h"
#include "Monitor.h"
#include "device/handle.hpp"
#include "device/hwcnt/block_extents.hpp"
#include "device/hwcnt/block_metadata.hpp"
#include "device/hwcnt/prfcnt_set.hpp"
#include "device/hwcnt/sample.hpp"
#include "device/hwcnt/sampler/configuration.hpp"
#include "device/hwcnt/sampler/periodic.hpp"
#include "device/instance.hpp"
#include "mali_userspace/MaliDevice.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace mali_userspace {

    namespace dev = hwcpipe::device;
    namespace hwcnt = dev::hwcnt;

    namespace {
        hwcnt::sampler::periodic create_sampler(dev::instance & instance, std::uint32_t sample_period)
        {
            using namespace hwcpipe::device::hwcnt;

            sampler::configuration::enable_map_type enable_map {};
            enable_map.set();

            constexpr auto num_configs = static_cast<std::uint32_t>(block_extents::num_block_types);
            std::array<sampler::configuration, num_configs> configs {{
                {block_type::fe, prfcnt_set::primary, enable_map},
                {block_type::tiler, prfcnt_set::primary, enable_map},
                {block_type::memory, prfcnt_set::primary, enable_map},
                {block_type::core, prfcnt_set::primary, enable_map},
            }};

            return {instance, sample_period, configs.data(), configs.size()};
        }
    }

    MaliHwCntrTask::MaliHwCntrTask(std::unique_ptr<IBufferControl> buffer,
                                   std::unique_ptr<IBlockCounterFrameBuilder> frameBuilder,
                                   std::int32_t deviceNumber,
                                   IMaliDeviceCounterDumpCallback & callback_,
                                   const MaliDevice & device,
                                   std::map<CounterKey, int64_t> constantValues)
        : mBuffer(std::move(buffer)),
          mFrameBuilder(std::move(frameBuilder)),
          mCallback(callback_),
          mDevice(device),
          deviceNumber(deviceNumber),
          mConstantValues(std::move(constantValues))
    {
        handle = dev::handle::create(deviceNumber);
        if (!handle) {
            LOG_ERROR("Failed to create hwcpipe handle for device %d", deviceNumber);
            handleException();
        }

        instance = dev::instance::create(*handle);
        if (!instance) {
            LOG_ERROR("Failed to create hwcpipe instance for device %d", deviceNumber);
            handleException();
        }

        if (pipe2(interrupt_fd.data(), O_CLOEXEC) < 0) {
            LOG_ERROR("Could not create task interrupt pipe");
            handleException();
        }
    }

    void MaliHwCntrTask::interrupt()
    {
        char buf = 1;
        if (::write(interrupt_fd[1], &buf, 1) < 1) {
            LOG_ERROR("Could not interrupt counter task for GPU device number %d", deviceNumber);
            handleException();
        }
    }

    void MaliHwCntrTask::execute(int sampleRate,
                                 bool isOneShot,
                                 std::uint64_t monotonicStarted,
                                 const std::function<void()> & endSession)
    {
        // set sample interval, if sample rate == 0, then sample at 100Hz as currently the job dumping based sampling does not work... (driver issue?)
        const uint32_t sampleIntervalNs =
            (sampleRate > 0 ? (sampleRate < 1000000000 ? (1000000000U / sampleRate) : 1U) : 10000000U);

        auto sampler = create_sampler(*instance, sampleIntervalNs);
        if (!sampler) {
            LOG_ERROR("GPU sampler could not be initialized for device number %d", deviceNumber);
            handleException();
        }
        auto & reader = sampler.get_reader();

        if (!mConstantValues.empty()) {
            bool wroteConstants = writeConstants();
            if (!wroteConstants) {
                LOG_ERROR("Failed to send constants for device %d", deviceNumber);
                mFrameBuilder->flush();
                mBuffer->setDone();
                return;
            }
        }

        Monitor monitor;
        if (!monitor.init() || !monitor.add(reader.get_fd()) || !monitor.add(interrupt_fd[0])) {
            LOG_ERROR("Failed to set up epoll monitor for GPU sampler on device %d", deviceNumber);
            handleException();
        }

        sampler.sampling_start(0);

        // create the list of enabled counters
        const MaliDeviceCounterList countersList(mDevice.createCounterList(mCallback));
        while (true) {
            epoll_event event;
            int ready = monitor.wait(&event, 1, -1);
            if (ready < 0) {
                LOG_ERROR("Epoll wait failed for GPU device %d", deviceNumber);
                break;
            }

            if (ready == 0) {
                continue;
            }

            if (event.data.fd == reader.get_fd()) {
                auto ec = write_sample(countersList, reader, monotonicStarted);
                if (ec) {
                    LOG_ERROR("Error getting Mali counter sample on device %d: %s", deviceNumber, ec.message().c_str());
                    handleException();
                }
            }
            else if (event.data.fd == interrupt_fd[0]) {
                break;
            }

            if (isOneShot && (mBuffer->isFull())) {
                LOG_DEBUG("One shot (malihwc)");
                endSession();
            }
        }

        sampler.sampling_stop(0);
        mFrameBuilder->flush();
        mBuffer->setDone();
    }

    std::error_code MaliHwCntrTask::write_sample(const MaliDeviceCounterList & counter_list,
                                                 hwcnt::reader & reader,
                                                 std::uint64_t monotonic_start)
    {
        std::error_code ec;
        hwcnt::sample sample(reader, ec);
        if (ec) {
            return ec;
        }

        const std::uint64_t sample_time = sample.get_metadata().timestamp_ns_end - monotonic_start;
        if (mFrameBuilder->eventHeader(sample_time) && mFrameBuilder->eventCore(deviceNumber)) {

            mDevice.dumpCounters(counter_list,
                                 sample,
                                 reader.get_features().has_block_state,
                                 *mFrameBuilder,
                                 mCallback);
            mFrameBuilder->check(sample_time);
        }

        return {};
    }

    bool MaliHwCntrTask::write(ISender & sender)
    {
        return mBuffer->write(sender);
    }

    bool MaliHwCntrTask::writeConstants()
    {
        constexpr uint64_t constantsTimestamp = 0;
        if (mFrameBuilder->eventHeader(constantsTimestamp) && mFrameBuilder->eventCore(deviceNumber)) {
            for (const auto & pair : mConstantValues) {
                const auto & keyOfConstant = pair.first;
                const int64_t value = pair.second;

                if (!mFrameBuilder->event64(keyOfConstant, value)) {
                    return false;
                }
            }
            mFrameBuilder->flush();
            return true;
        }
        return false;
    }
}
