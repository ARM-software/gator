/* Copyright (C) 2019-2022 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_

#include "DynBuf.h"
#include "Logging.h"
#include "MaliGPUClockPolledDriverCounter.h"
#include "PolledDriver.h"
#include <mxml.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

namespace mali_userspace {

    class MaliGPUClockPolledDriver : public PolledDriver {
    public:
        MaliGPUClockPolledDriver(std::string clockPath, unsigned deviceNumber);

        // Intentionally unimplemented
        MaliGPUClockPolledDriver(const MaliGPUClockPolledDriver &) = delete;
        MaliGPUClockPolledDriver & operator=(const MaliGPUClockPolledDriver &) = delete;
        MaliGPUClockPolledDriver(MaliGPUClockPolledDriver &&) = delete;
        MaliGPUClockPolledDriver & operator=(MaliGPUClockPolledDriver &&) = delete;

        void readEvents(mxml_node_t * const /*root*/) override;

        int writeCounters(mxml_node_t * root) const override;

        void start() override {}
        void read(IBlockCounterFrameBuilder & buffer) override;
        void writeEvents(mxml_node_t * root) const override;

    private:
        static constexpr std::string_view ARM_MALI_CLOCK = "ARM_Mali-clock-";

        std::string mClockPath;
        unsigned deviceNumber;
        std::string counterName;
        uint64_t mClockValue {0};
        DynBuf mBuf {};

        bool doRead();
    };
}
#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_ */
