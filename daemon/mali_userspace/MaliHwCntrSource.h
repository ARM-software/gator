/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
#define NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_

#include <semaphore.h>

#include "mali_userspace/MaliDevice.h"

#include "Buffer.h"
#include "Source.h"

namespace mali_userspace
{
    class MaliHwCntrSource : public Source, private virtual IMaliDeviceCounterDumpCallback
    {
    public:
        MaliHwCntrSource(sem_t *senderSem);
        ~MaliHwCntrSource();

        bool prepare();
        void run();
        void interrupt();

        bool isDone();
        void write(Sender *sender);

    private:

        Buffer mBuffer;

        // Intentionally unimplemented
        MaliHwCntrSource(const MaliHwCntrSource &);
        MaliHwCntrSource &operator=(const MaliHwCntrSource &);

        virtual void nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t delta);
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex) const;
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
