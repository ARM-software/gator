/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_

#include "PolledDriver.h"
#include "SessionData.h"
#include "SimpleDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
namespace mali_userspace {
    /**
     * Implements a counter driver for all Mali Midgard devices with r8p0 or later driver installed.
     * Allows reading counters from userspace, without modification to mali driver
     */
    class MaliHwCntrDriver : public SimpleDriver {
        using super = SimpleDriver;

    public:
        MaliHwCntrDriver();

        // Intentionally unimplemented
        MaliHwCntrDriver(const MaliHwCntrDriver &) = delete;
        MaliHwCntrDriver & operator=(const MaliHwCntrDriver &) = delete;
        MaliHwCntrDriver(MaliHwCntrDriver &&) = delete;
        MaliHwCntrDriver & operator=(MaliHwCntrDriver &&) = delete;

        bool claimCounter(Counter & counter) const override;
        void resetCounters() override;
        void setupCounter(Counter & counter) override;
        bool start();

        inline const std::map<unsigned, std::unique_ptr<PolledDriver>> & getPolledDrivers() const
        {
            return mPolledDrivers;
        }

        inline const std::map<unsigned, std::unique_ptr<MaliDevice>> & getDevices() const { return mDevices; }

        void insertConstants(std::set<Constant> & dest) override;
        int getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const;

        const char * getSupportedDeviceFamilyName() const;

        /** @return map from device number to gpu id */
        std::map<unsigned, unsigned> getDeviceGpuIds() const;

    private:
        /** For each possible counter index, contains the counter key, or 0 if not enabled
         * Mapped to the GPUID not device number as counters are common across all devices with the same type.
         */
        std::map<unsigned, std::unique_ptr<int[]>> mEnabledCounterKeysByGpuId {};
        /** Map of the GPU device number and Polling driver for GPU clock etc. */
        std::map<unsigned, std::unique_ptr<PolledDriver>> mPolledDrivers {};
        //Map between the device number and the mali devices .
        std::map<unsigned, std::unique_ptr<MaliDevice>> mDevices;
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
