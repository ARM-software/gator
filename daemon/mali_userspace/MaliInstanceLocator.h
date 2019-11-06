/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_

#include "mali_userspace/MaliDevice.h"

#include <map>
#include <memory>

namespace mali_userspace
{

    /**
     * Scan the system for Mali devices and create MaliHwCntrDrivers for all Mali devices

     * @return a map of the core ID and the mali device for the cores.
     */
    std::map<unsigned int, std::unique_ptr<MaliDevice>> enumerateAllMaliHwCntrDrivers();
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_ */
