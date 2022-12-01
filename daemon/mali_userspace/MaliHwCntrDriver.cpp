/* Copyright (C) 2016-2022 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrDriver.h"

#include "Counter.h"
#include "Logging.h"
#include "MaliGPUClockPolledDriver.h"
#include "MaliHwCntr.h"
#include "SessionData.h"
#include "capture/Environment.h"
#include "lib/perfetto_utils.h"
#include "mali_userspace/MaliInstanceLocator.h"

#include <algorithm>
#include <cstdlib>

#include <unistd.h>

namespace mali_userspace {
    MaliHwCntrDriver::MaliHwCntrDriver()
        : SimpleDriver("MaliHwCntrDriver"), mDevices(mali_userspace::enumerateAllMaliHwCntrDrivers())
    {
        if (mDevices.empty()) {
            LOG_DEBUG("There are no mali devices to create readers");
            return;
        }

        //Add counters done only once.
        std::vector<uint32_t> addedGpuIds;
        for (auto const & device : mDevices) {
            const MaliDevice & t_device = *device.second;
            const uint32_t gpuId = t_device.getGpuId();

            if (std::find(addedGpuIds.begin(), addedGpuIds.end(), gpuId)
                == addedGpuIds.end()) { // finds if counter already created for gpu
                // add all the device counters
                const uint32_t numNameBlocks = t_device.getNameBlockCount();

                // allocate the enable map
                const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
                std::unique_ptr<int[]> mEnabledCounterKeys(new int[enabledMapLength] {});

                for (uint32_t nameBlockIndex = 0; nameBlockIndex < numNameBlocks; ++nameBlockIndex) {
                    for (uint32_t counterIndex = 0; counterIndex < MaliDevice::NUM_COUNTERS_PER_BLOCK; ++counterIndex) {
                        // get the next counter name
                        const char * counterName = t_device.getCounterName(nameBlockIndex, counterIndex);
                        if (counterName == nullptr) {
                            continue;
                        }
                        // create a counter object for it
                        char * name;
                        if (asprintf(&name, "ARM_Mali-%s", counterName) <= 0) {
                            LOG_ERROR("asprintf failed");
                            handleException();
                        }

                        LOG_DEBUG("Added counter '%s' @ %u %u", name, nameBlockIndex, counterIndex);
                        setCounters(new MaliHwCntr(getCounters(), name, nameBlockIndex, counterIndex, gpuId));
                        ::free(name);
                    }
                }
                mEnabledCounterKeysByGpuId[gpuId] = std::move(mEnabledCounterKeys);
                addedGpuIds.push_back(gpuId);
            }
        }

        // add GPU clock driver
        for (auto const & mDevice : mDevices) {
            const MaliDevice & device = *mDevice.second;
            if (!device.getClockPath().empty()) {
                mPolledDrivers[mDevice.first] =
                    std::unique_ptr<PolledDriver>(new MaliGPUClockPolledDriver(device.getClockPath(), mDevice.first));
            }
            else {
                LOG_SETUP("Mali GPU counters\nGPU frequency counters not available for GPU # %d.", mDevice.first);
            }
        }
    }

    bool MaliHwCntrDriver::start() { return true; }

    bool MaliHwCntrDriver::claimCounter(Counter & counter) const
    {
        // do not claim if another driver already has
        if (counter.getDriver() != nullptr) {
            return false;
        }
        return super::claimCounter(counter);
    }

    void MaliHwCntrDriver::resetCounters()
    {
        for (auto const & device : mDevices) {
            const uint32_t numNameBlocks = device.second->getNameBlockCount();
            const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
            for (auto & counterKeys : mEnabledCounterKeysByGpuId) {
                std::unique_ptr<int[]> & counterKey = counterKeys.second;
                std::fill(counterKey.get(), counterKey.get() + enabledMapLength, 0);
            }
        }
        super::resetCounters();
    }

    void MaliHwCntrDriver::setupCounter(Counter & counter)
    {
        auto * const malihwcCounter = static_cast<mali_userspace::MaliHwCntr *>(findCounter(counter));
        if (malihwcCounter == nullptr) {
            counter.setEnabled(false);
            return;
        }
        uint32_t gpuId = malihwcCounter->getGpuId();
        const int32_t index = (malihwcCounter->getNameBlockIndex() * MaliDevice::NUM_COUNTERS_PER_BLOCK
                               + malihwcCounter->getCounterIndex());
        if (mEnabledCounterKeysByGpuId.find(gpuId) != mEnabledCounterKeysByGpuId.end()) {
            mEnabledCounterKeysByGpuId.find(gpuId)->second[index] = malihwcCounter->getKey();
        }
        malihwcCounter->setEnabled(true);
        counter.setKey(malihwcCounter->getKey());
    }

    void MaliHwCntrDriver::insertConstants(std::set<Constant> & dest) { MaliDevice::insertConstants(dest); }

    int MaliHwCntrDriver::getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const
    {
        if (counterIndex < MaliDevice::NUM_COUNTERS_PER_BLOCK) {
            const uint32_t index = (nameBlockIndex * MaliDevice::NUM_COUNTERS_PER_BLOCK + counterIndex);
            const auto it = mEnabledCounterKeysByGpuId.find(gpuId);
            if (it == mEnabledCounterKeysByGpuId.end()) {
                return 0;
            }
            return it->second[index];
        }
        return 0;
    }

    const char * MaliHwCntrDriver::getSupportedDeviceFamilyName() const
    {
        if (!mDevices.empty()) {
            //TODO: return it for first, for the time being
            const char * supportedDevice = mDevices.begin()->second->getSupportedDeviceFamilyName();
            return supportedDevice;
        }
        return nullptr;
    }

    std::map<unsigned, unsigned> MaliHwCntrDriver::getDeviceGpuIds() const
    {
        std::map<unsigned, unsigned> result;
        for (const auto & device : mDevices) {
            result[device.first] = device.second->getGpuId();
        }
        return result;
    }

}
