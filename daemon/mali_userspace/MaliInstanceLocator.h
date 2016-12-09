/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_

#include "mali_userspace/MaliDevice.h"

namespace mali_userspace
{
    /**
     * Scan the system for Mali devices and create MaliHwCntrDrivers for any Mali devices
     */
    MaliDevice * enumerateMaliHwCntrDrivers();
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIINSTANCELOCATOR_H_ */
