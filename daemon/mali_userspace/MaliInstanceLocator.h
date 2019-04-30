/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_

#include "mali_userspace/MaliDevice.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
namespace mali_userspace
{

    /**
     * Scan the system for Mali devices and create MaliHwCntrDrivers for all Mali devices
     *
     * @param userSpecifiedDeviceType A vector of strings containing the model name or gpuid number.
     *                                When not empty, it is used to hard code the GPU type when it cannot be detected automatically
     * @param userSpecifiedDevicePath A vector of strings containing the path(s) to the mali device.
     *                                The order of this vector should match that of userSpecifiedDeviceType, unless all devices are of the same
     *                                type, in which case only one type is needed.
     *                                If empty will default to "/dev/mali0", when it cannot be detected automatically
     * @return a map of the core ID and the mali device for the cores.
     */
    std::map<unsigned int, std::unique_ptr<MaliDevice>> enumerateAllMaliHwCntrDrivers(
            std::vector<std::string> userSpecifiedDeviceType, std::vector<std::string> userSpecifiedDevicePath);
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_ */
