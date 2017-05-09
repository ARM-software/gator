/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrDriver.h"

#include "mali_userspace/MaliInstanceLocator.h"
#include "Counter.h"
#include "Logging.h"

namespace mali_userspace
{
    namespace
    {
        class MaliHwCntr : public DriverCounter
        {
        public:

            MaliHwCntr(DriverCounter * next, const char * name, uint32_t nameBlockIndex, uint32_t counterIndex)
                    : DriverCounter(next, name),
                      mNameBlockIndex(nameBlockIndex),
                      mCounterIndex(counterIndex)
            {
            }

            inline uint32_t getNameBlockIndex() const
            {
                return mNameBlockIndex;
            }

            inline uint32_t getCounterIndex() const
            {
                return mCounterIndex;
            }

        private:
            uint32_t mNameBlockIndex;
            uint32_t mCounterIndex;

            // Intentionally undefined
            CLASS_DELETE_COPY_MOVE(MaliHwCntr);
        };
    }

    MaliHwCntrDriver::MaliHwCntrDriver()
        :   mReader (NULL),
            mEnabledCounterKeys (NULL)
    {
        query();
    }

    MaliHwCntrDriver::~MaliHwCntrDriver()
    {
        if (mReader != NULL) {
            delete mReader;
        }

        if (mEnabledCounterKeys != NULL) {
            delete mEnabledCounterKeys;
        }
    }

    bool MaliHwCntrDriver::query()
    {
        bool addCounters;
        const MaliDevice * device;

        // only do it once
        if (mReader != NULL) {
            device = MaliHwCntrReader::freeReaderRetainDevice(mReader);
            addCounters = false;
        }
        else {
            // query for the device
            device = mali_userspace::enumerateMaliHwCntrDrivers();
            if (device == NULL) {
                return false;
            }
            addCounters = true;
        }

        // create reader
        mReader = MaliHwCntrReader::create(device);
        if ((mReader == NULL) || (!mReader->isInitialized())) {
            return false;
        }

        // do we need to add counters?
        if (!addCounters)
        {
            return false;
        }

        // add all the device counters
        const uint32_t numNameBlocks = device->getNameBlockCount();

        // allocate the enable map
        const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
        mEnabledCounterKeys = new int[enabledMapLength];
        memset(mEnabledCounterKeys, 0, enabledMapLength * sizeof(mEnabledCounterKeys[0]));

        for (uint32_t nameBlockIndex = 0; nameBlockIndex < numNameBlocks; ++nameBlockIndex)
        {
            for (uint32_t counterIndex = 0; counterIndex < MaliDevice::NUM_COUNTERS_PER_BLOCK; ++counterIndex) {
                // get the next counter name
                const char * counterName = device->getCounterName(nameBlockIndex, counterIndex);
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

                setCounters(new MaliHwCntr(getCounters(), name, nameBlockIndex, counterIndex));
            }
        }

        return true;
    }

    bool MaliHwCntrDriver::start()
    {
        return true;
    }

    bool MaliHwCntrDriver::claimCounter(const Counter & counter) const
    {
        // do not claim if another driver already has
        if (counter.getDriver() != NULL) {
            return false;
        }

        return super::claimCounter(counter);
    }

    void MaliHwCntrDriver::resetCounters()
    {
        if ((!query()) && (mReader != NULL) && (mEnabledCounterKeys != NULL)) {
            const uint32_t numNameBlocks = mReader->getDevice().getNameBlockCount();
            const uint32_t enabledMapLength = (numNameBlocks * MaliDevice::NUM_COUNTERS_PER_BLOCK);
            memset(mEnabledCounterKeys, 0, enabledMapLength * sizeof(mEnabledCounterKeys[0]));
        }

        super::resetCounters();
    }

    void MaliHwCntrDriver::setupCounter(Counter &counter)
    {
        MaliHwCntr * const malihwcCounter = static_cast<MaliHwCntr *>(findCounter(counter));
        if (malihwcCounter == NULL) {
            counter.setEnabled(false);
            return;
        }

        const uint32_t index = (malihwcCounter->getNameBlockIndex() * MaliDevice::NUM_COUNTERS_PER_BLOCK + malihwcCounter->getCounterIndex());

        mEnabledCounterKeys[index] = malihwcCounter->getKey();

        malihwcCounter->setEnabled(true);
        counter.setKey(malihwcCounter->getKey());
    }

    int MaliHwCntrDriver::getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex) const
    {
        if (mEnabledCounterKeys != NULL) {
            const uint32_t index = (nameBlockIndex * MaliDevice::NUM_COUNTERS_PER_BLOCK + counterIndex);
            return mEnabledCounterKeys[index];
        }

        return 0;
    }

    const char * MaliHwCntrDriver::getSupportedDeviceFamilyName() const
    {
        const_cast<MaliHwCntrDriver *>(this)->query();

        if (mReader != NULL) {
            return mReader->getDevice().getSupportedDeviceFamilyName();
        }

        return NULL;
    }
}
