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

#include "ClassBoilerPlate.h"
#include "Buffer.h"
#include "Source.h"

namespace mali_userspace
{
    class MaliHwCntrSource : public Source, private virtual IMaliDeviceCounterDumpCallback
    {
    public:
        MaliHwCntrSource(Child & child, sem_t *senderSem);
        ~MaliHwCntrSource();

        virtual bool prepare() override;
        virtual void run() override;
        virtual void interrupt() override;
        virtual bool isDone() override;
        virtual void write(Sender * sender) override;

    private:

        Buffer mBuffer;

        // Intentionally unimplemented
        CLASS_DELETE_COPY_MOVE(MaliHwCntrSource);

        virtual void nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta);
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex) const;
    };
}

#endif // NATIVE_GATOR_DAEMON_MIDGARDHWCOUNTERSOURCE_H_
