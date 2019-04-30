/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_

#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include "DynBuf.h"
#include "Logging.h"

#include "PolledDriver.h"
#include "MaliGPUClockPolledDriverCounter.h"

namespace mali_userspace {

    class MaliGPUClockPolledDriver : public PolledDriver
    {
    private:

        typedef PolledDriver super;

    public:

        MaliGPUClockPolledDriver(std::string clockPath)
                : PolledDriver("MaliGPUClock"),
                  mClockPath(clockPath),
                  mClockValue(0),
                  mBuf()
        {
            logg.logMessage("GPU CLOCK POLLING '%s'", mClockPath.c_str());
        }

        // Intentionally unimplemented
        CLASS_DELETE_COPY_MOVE(MaliGPUClockPolledDriver)
        ;

        void readEvents(mxml_node_t * const /*root*/)
        {
            if (access(mClockPath.c_str(), R_OK) == 0) {
                logg.logSetup("Mali GPU counters\nAccess %s is OK. GPU frequency counters available.", mClockPath.c_str());
                setCounters(new mali_userspace::MaliGPUClockPolledDriverCounter(getCounters(), "ARM_Mali-clock", mClockValue));
            }
            else {

                logg.logSetup("Mali GPU counters\nCannot access %s. GPU frequency counters not available.", mClockPath.c_str());
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

        std::string mClockPath;
        uint64_t mClockValue;
        DynBuf mBuf;

        bool doRead()
        {
            if (!countersEnabled()) {
                return true;
            }

            if (!mBuf.read(mClockPath.c_str())) {
                return false;
            }

            mClockValue = strtoull(mBuf.getBuf(), nullptr, 0) * 1000000ull;
            return true;
        }
    };
}
#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_ */
