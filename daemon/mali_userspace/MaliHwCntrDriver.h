/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_

#include "ClassBoilerPlate.h"
#include "PolledDriver.h"
#include "SimpleDriver.h"
#include "mali_userspace/MaliHwCntrReader.h"
#include <map>
#include <memory>
#include <vector>
#include "lib/Optional.h"
namespace mali_userspace
{
    /**
     * Implements a counter driver for all Mali Midgard devices with r8p0 or later driver installed.
     * Allows reading counters from userspace, without modification to mali driver, or installation of gator.ko
     */
    class MaliHwCntrDriver : public SimpleDriver
    {
        typedef SimpleDriver super;

    public:

        MaliHwCntrDriver(const std::vector<std::string> userSpecifiedDeviceTypes,
                         const std::vector<std::string> userSpecifiedDevicePaths);

        bool claimCounter(Counter &counter) const;
        void resetCounters();
        void setupCounter(Counter &counter) override;
        bool start();


        inline const std::map<unsigned, std::unique_ptr<MaliHwCntrReader>>& getReaders() const
        {
            return mReaders;
        }
        inline const std::map<unsigned, std::unique_ptr<PolledDriver>>& getPolledDrivers() const
        {
            return mPolledDrivers;
        }
        int getCounterKey(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const;

        const char * getSupportedDeviceFamilyName() const;

        /** @return map from device number to gpu id */
        std::map<unsigned, unsigned> getDeviceGpuIds() const;

    private:

        /** User specified device type vector */
        const std::vector<std::string> mUserSpecifiedDeviceTypes;
        /** User specified device path vector */
        const std::vector<std::string> mUserSpecifiedDevicePaths;

        /**Map of GPU device number and mali hw counter reader */
        std::map<unsigned,  std::unique_ptr<MaliHwCntrReader>> mReaders;
        /** For each possible counter index, contains the counter key, or 0 if not enabled
         * Mapped to the GPUID not device number as counters are common across all devices with the same type.
         */
        std::map<unsigned,  std::unique_ptr<int[]>> mEnabledCounterKeysByGpuId;
        /** Map of the GPU device number and Polling driver for GPU clock etc. */
        std::map<unsigned, std::unique_ptr<PolledDriver>> mPolledDrivers;
        //Map between the device number and the mali devices .
        std::map<unsigned, std::unique_ptr<MaliDevice>> mDevices;
        /**
         * initialize/ reinitialize readers
         */
        void query();
        // Intentionally unimplemented
        CLASS_DELETE_COPY_MOVE(MaliHwCntrDriver);
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERDRIVER_H_
