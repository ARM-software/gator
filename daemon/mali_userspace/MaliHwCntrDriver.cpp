/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrDriver.h"

#include "mali_userspace/MaliInstanceLocator.h"
#include "Counter.h"
#include "Logging.h"
#include "MaliHwCntr.h"
#include "MaliGPUClockPolledDriver.h"

#include <cstdlib>
#include <unistd.h>
#include <algorithm>

namespace mali_userspace
{
    MaliHwCntrDriver::MaliHwCntrDriver(const std::vector<std::string> userSpecifiedDeviceTypes,
                                       const std::vector<std::string> userSpecifiedDevicePaths)
            : SimpleDriver("MaliHwCntrDriver"),
              mUserSpecifiedDeviceTypes(userSpecifiedDeviceTypes),
              mUserSpecifiedDevicePaths(userSpecifiedDevicePaths),
              mReaders(),
              mEnabledCounterKeysByGpuId(),
              mPolledDrivers(),
              mDevices()
    {
        query();

        // add GPU clock driver
        if (!mReaders.empty()){
            for (auto const& mDevice : mDevices) {
                const MaliDevice & device = *mDevice.second;
                if (!device.getClockPath().empty()) {
                    mPolledDrivers[mDevice.first] = std::unique_ptr<PolledDriver> (new MaliGPUClockPolledDriver( device.getClockPath()));
                } else {
                    logg.logSetup("GPU frequency counters not available for gpu id 0x%04x.", mDevice.first);
                }
            }
        }
    }
    void MaliHwCntrDriver::query()
    {
       if (mDevices.empty()) {
            mDevices = mali_userspace::enumerateAllMaliHwCntrDrivers(mUserSpecifiedDeviceTypes,
                                                                     mUserSpecifiedDevicePaths);
            if (mDevices.empty()) {
                logg.logMessage("There are no mali devices to create readers");
                return;
            }
            //Add counters done only once.
            std::vector<uint32_t> addedGpuIds;
            for (auto const& device : mDevices) {
                const MaliDevice& t_device = *device.second;
                const uint32_t gpuId = t_device.getGPUId();

                if (std::find(addedGpuIds.begin(), addedGpuIds.end(), gpuId) == addedGpuIds.end()) { // finds if counter already created for gpu
                        // add all the device counters
                    const uint32_t numNameBlocks = t_device.getNameBlockCount();

                    // allocate the enable map
                    const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
                    std::unique_ptr<int[]> mEnabledCounterKeys(new int[enabledMapLength] { });

                    for (uint32_t nameBlockIndex = 0; nameBlockIndex < numNameBlocks; ++nameBlockIndex) {
                        for (uint32_t counterIndex = 0; counterIndex < MaliDevice::NUM_COUNTERS_PER_BLOCK;
                                ++counterIndex) {
                            // get the next counter name
                            const char * counterName = t_device.getCounterName(nameBlockIndex, counterIndex);
                            if (counterName == NULL) {
                                continue;
                            }
                            // create a counter object for it
                            char *name;
                            if (asprintf(&name, "ARM_Mali-%s", counterName) <= 0) {
                                logg.logError("asprintf failed");
                                handleException();
                            }

                            logg.logMessage("Added counter '%s' @ %u %u", name, nameBlockIndex, counterIndex);
                            setCounters(new MaliHwCntr(getCounters(), name, nameBlockIndex, counterIndex, gpuId));
                            ::free(name);
                        }
                    }
                    mEnabledCounterKeysByGpuId[gpuId] = std::move(mEnabledCounterKeys);
                    addedGpuIds.push_back(gpuId);
                }
            }
       }
       // For every call to query recreate reader - because as follows
       // The reader is recreated as the ioctl interface is configured during the constructor,
       // if we need to configure different counter selection then constructor
       // must be run again on new Reader object
       //Not relying on the key as to map between device and reader
       mReaders.clear();
       for (auto & device : mDevices) {
           std::unique_ptr<MaliHwCntrReader> mReader = MaliHwCntrReader::createReader(*device.second);
           if (!mReader ) {
               logg.logError("Create reader failed for device %s for GPUId 0x%04X ", device.second->getDevicePath().c_str(), device.second->getGPUId());
               continue;
           }
           if(!mReader->isInitialized()) {
               logg.logMessage("Reader created, but initialized failed. ");
           }
           mReaders[device.first] = std::move(mReader);
       }
    }

    bool MaliHwCntrDriver::start()
    {
        return true;
    }

    bool MaliHwCntrDriver::claimCounter(Counter & counter) const
    {
        // do not claim if another driver already has
        if (counter.getDriver() != NULL) {
            return false;
        }
        return super::claimCounter(counter);
    }

    void MaliHwCntrDriver::resetCounters()
    {
        query();
        for (auto const& reader : mReaders) {
            const uint32_t numNameBlocks = reader.second->getDevice().getNameBlockCount();
            const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
            for (auto & counterKeys : mEnabledCounterKeysByGpuId) {
                std::unique_ptr<int[]> & counterKey = counterKeys.second;
                std::fill(counterKey.get(), counterKey.get() + enabledMapLength, 0);
            }
        }
        super::resetCounters();
    }

    void MaliHwCntrDriver::setupCounter(Counter &counter)
    {
        mali_userspace::MaliHwCntr * const malihwcCounter = static_cast<mali_userspace::MaliHwCntr *>(findCounter(counter));
        if (malihwcCounter == NULL) {
            counter.setEnabled(false);
            return;
        }
        uint32_t gpuId = malihwcCounter->getGpuId();
        const int32_t index = (malihwcCounter->getNameBlockIndex() * MaliDevice::NUM_COUNTERS_PER_BLOCK + malihwcCounter->getCounterIndex());
        if (mEnabledCounterKeysByGpuId.find(gpuId) != mEnabledCounterKeysByGpuId.end()) {
            mEnabledCounterKeysByGpuId.find(gpuId)->second[index] = malihwcCounter->getKey();
        }
        malihwcCounter->setEnabled(true);
        counter.setKey(malihwcCounter->getKey());
    }

    int MaliHwCntrDriver::getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const
    {
        uint32_t numNameBlock = 0;
        for(auto const& device : mDevices) {
            if(device.second->getGPUId() == gpuId) {
                //Find the name block count
                numNameBlock = device.second->getNameBlockCount();
                break;
            }
        }
        if(nameBlockIndex < numNameBlock && counterIndex < MaliDevice::NUM_COUNTERS_PER_BLOCK) {
            const uint32_t index = (nameBlockIndex * MaliDevice::NUM_COUNTERS_PER_BLOCK + counterIndex);
            if (mEnabledCounterKeysByGpuId.find(gpuId) == mEnabledCounterKeysByGpuId.end()) {
                return 0;
            }
            return mEnabledCounterKeysByGpuId.find(gpuId)->second[index];
        }
        return 0;
    }

    const char * MaliHwCntrDriver::getSupportedDeviceFamilyName() const
    {
        const_cast<MaliHwCntrDriver *>(this)->query();
        if (!mDevices.empty()) {
            //TODO: return it for first, for the time being
            const char* supportedDevice = mDevices.begin()->second->getSupportedDeviceFamilyName();
            return supportedDevice;
        }
        return NULL;
    }

    std::map<unsigned, unsigned> MaliHwCntrDriver::getDeviceGpuIds() const
    {
        std::map<unsigned, unsigned> result;
        for (const auto & device : mDevices) {
            result[device.first] = device.second->getGPUId();
        }
        return result;
    }
}
