/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#pragma once

#include <memory>

#include <semaphore.h>
class Source;

namespace mali_userspace {
    class MaliHwCntrDriver;
    std::shared_ptr<Source> createMaliHwCntrSource(sem_t & senderSem, MaliHwCntrDriver & driver);
}
