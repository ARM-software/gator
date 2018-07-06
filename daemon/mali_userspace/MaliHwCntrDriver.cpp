/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrDriver.h"

#include "mali_userspace/MaliInstanceLocator.h"
#include "Counter.h"
#include "Logging.h"

#include <cstdlib>
#include <unistd.h>

namespace mali_userspace
{
    namespace
    {
        class MaliHwCntr : public DriverCounter
        {
        public:

            MaliHwCntr(DriverCounter * next, const char * name, int32_t nameBlockIndex, int32_t counterIndex)
                    : DriverCounter(next, name),
                      mNameBlockIndex(nameBlockIndex),
                      mCounterIndex(counterIndex)
            {
            }

            inline int32_t getNameBlockIndex() const
            {
                return mNameBlockIndex;
            }

            inline int32_t getCounterIndex() const
            {
                return mCounterIndex;
            }

        private:
            int32_t mNameBlockIndex;
            int32_t mCounterIndex;

            // Intentionally undefined
            CLASS_DELETE_COPY_MOVE(MaliHwCntr);
        };

        class MaliGPUClockPolledDriverCounter : public DriverCounter
        {
        public:
            MaliGPUClockPolledDriverCounter(DriverCounter *next, char * const name, uint64_t & value)
                    : DriverCounter(next, name),
                      mValue(value)
            {
            }
            ~MaliGPUClockPolledDriverCounter()
            {
            }

            int64_t read()
            {
                return mValue;
            }

        private:
            uint64_t & mValue;

            // Intentionally unimplemented
            CLASS_DELETE_COPY_MOVE(MaliGPUClockPolledDriverCounter);
        };

        class MaliGPUClockPolledDriver : public PolledDriver
        {
        private:

            typedef PolledDriver super;

        public:

            MaliGPUClockPolledDriver(const char * clockPath)
                    : mClockPath(clockPath),
                      mClockValue(0),
                      mBuf()
            {
                logg.logMessage("GPU CLOCK POLLING '%s'", clockPath);
            }

            // Intentionally unimplemented
            CLASS_DELETE_COPY_MOVE(MaliGPUClockPolledDriver);

            void readEvents(mxml_node_t * const /*root*/)
            {
                if (access(mClockPath, R_OK) == 0) {
                    logg.logSetup("Mali GPU counters\nAccess %s is OK. GPU frequency counters available.", mClockPath);
                    setCounters(new MaliGPUClockPolledDriverCounter(getCounters(), strdup("ARM_Mali-clock"), mClockValue));
                }
                else {
                    logg.logSetup("Mali GPU counters\nCannot access %s. GPU frequency counters not available.", mClockPath);
                }
            }

            void start()
            {
            }

            void read(Buffer * const buffer)
            {
                if (!doRead()) {
                    logg.logError("Unable to read GPU clock frequency");
                    handleException();
                }
                super::read(buffer);
            }

        private:

            const char * const mClockPath;
            uint64_t mClockValue;
            DynBuf mBuf;

            bool doRead()
            {
                if (!countersEnabled()) {
                    return true;
                }

                if (!mBuf.read(mClockPath)) {
                    return false;
                }

                mClockValue = strtoull(mBuf.getBuf(), nullptr, 0) * 1000000ull;
                return true;
            }
        };
    }

    MaliHwCntrDriver::MaliHwCntrDriver()
        :   mUserSpecifiedDeviceType(),
            mUserSpecifiedDevicePath(),
            mReader (NULL),
            mEnabledCounterKeys (NULL),
            mPolledDriver (nullptr)
    {
    }

    void MaliHwCntrDriver::initialize(const char * userSpecifiedDeviceType, const char * userSpecifiedDevicePath)
    {
        this->mUserSpecifiedDeviceType = userSpecifiedDeviceType;
        this->mUserSpecifiedDevicePath = userSpecifiedDevicePath;

        query();

        // add GPU clock driver
        if (mReader != nullptr){
            const MaliDevice & device = mReader->getDevice();
            const char * const clockPath = device.getClockPath();
            if (clockPath != nullptr) {
                mPolledDriver = new MaliGPUClockPolledDriver(clockPath);
            }
            else {
                logg.logSetup("Mali GPU counters\nGPU frequency counters not available.");
            }
        }
    }

    MaliHwCntrDriver::~MaliHwCntrDriver()
    {
        if (mReader != NULL) {
            delete mReader;
        }

        if (mEnabledCounterKeys != NULL) {
            delete mEnabledCounterKeys;
        }

        if (mPolledDriver != nullptr) {
            delete mPolledDriver;
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
            device = mali_userspace::enumerateMaliHwCntrDrivers(mUserSpecifiedDeviceType, mUserSpecifiedDevicePath);
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

        const int32_t index = (malihwcCounter->getNameBlockIndex() * MaliDevice::NUM_COUNTERS_PER_BLOCK + malihwcCounter->getCounterIndex());

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
