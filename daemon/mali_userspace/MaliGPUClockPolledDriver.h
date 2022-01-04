/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_

#include "DynBuf.h"
#include "Logging.h"
#include "MaliGPUClockPolledDriverCounter.h"
#include "PolledDriver.h"

#include <cstdint>
#include <cstdlib>
#include <utility>

#include <unistd.h>

namespace mali_userspace {

    class MaliGPUClockPolledDriver : public PolledDriver {
    private:
        using super = PolledDriver;

    public:
        MaliGPUClockPolledDriver(std::string clockPath) : PolledDriver("MaliGPUClock"), mClockPath(std::move(clockPath))
        {
            LOG_DEBUG("GPU CLOCK POLLING '%s'", mClockPath.c_str());
        }

        // Intentionally unimplemented
        MaliGPUClockPolledDriver(const MaliGPUClockPolledDriver &) = delete;
        MaliGPUClockPolledDriver & operator=(const MaliGPUClockPolledDriver &) = delete;
        MaliGPUClockPolledDriver(MaliGPUClockPolledDriver &&) = delete;
        MaliGPUClockPolledDriver & operator=(MaliGPUClockPolledDriver &&) = delete;

        void readEvents(mxml_node_t * const /*root*/) override
        {
            if (access(mClockPath.c_str(), R_OK) == 0) {
                LOG_SETUP("Mali GPU counters\nAccess %s is OK. GPU frequency counters available.", mClockPath.c_str());
                setCounters(
                    new mali_userspace::MaliGPUClockPolledDriverCounter(getCounters(), "ARM_Mali-clock", mClockValue));
            }
            else {

                LOG_SETUP("Mali GPU counters\nCannot access %s. GPU frequency counters not available.",
                          mClockPath.c_str());
            }
        }

        void start() override {}

        void read(IBlockCounterFrameBuilder & buffer) override
        {
            if (!doRead()) {
                LOG_ERROR("Unable to read GPU clock frequency");
                handleException();
            }
            super::read(buffer);
        }

    private:
        std::string mClockPath;
        uint64_t mClockValue {0};
        DynBuf mBuf {};

        bool doRead()
        {
            if (!countersEnabled()) {
                return true;
            }

            if (!mBuf.read(mClockPath.c_str())) {
                return false;
            }

            mClockValue = strtoull(mBuf.getBuf(), nullptr, 0) * 1000000ULL;
            return true;
        }
    };
}
#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_ */
