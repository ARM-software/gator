/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_

#include "mali_userspace/MaliDevice.h"

namespace mali_userspace
{


    /**
     * Scan the system for Mali devices and create MaliHwCntrDrivers for any Mali devices
     *
     * @param userSpecifiedDeviceType A string containing the model name or gpuid number, or nullptr.
     *                                When specified, it is used to hard code the GPU type when it cannot be detected automatically
     * @param userSpecifiedDevicePath A path to the mali device, or nullptr.
     *                                If not specified will default to "/dev/mali0"
     */
    MaliDevice * enumerateMaliHwCntrDrivers(const char * userSpecifiedDeviceType, const char * userSpecifiedDevicePath);
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_ */
