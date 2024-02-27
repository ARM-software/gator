/* Copyright (C) 2019-2024 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_
#define MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_

#include "DynBuf.h"
#include "MaliGPUClockPolledDriverCounter.h"
#include "PolledDriver.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <mxml.h>
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

        [[nodiscard]] int writeCounters(available_counter_consumer_t const & consumer) const override;

        void start() override {}
        void read(IBlockCounterFrameBuilder & buffer) override;
        void writeEvents(mxml_node_t * root) const override;

    private:
        static constexpr std::string_view ARM_MALI_CLOCK = "ARM_Mali-clock-";

        // Anything below 10'000 is assumed to be in MHz
        static constexpr uint64_t MIN_RAW_kHz = 10'000;
        // Anything below 10'000'000 (and above 10'000) is assumed to be in kHz
        static constexpr uint64_t MAX_RAW_kHz = 10'000'000;
        static constexpr uint64_t ONE_MILLION = 1'000'000;
        static constexpr uint64_t ONE_THOUSAND = 1000;

        std::string mClockPath;
        unsigned deviceNumber;
        std::string counterName;
        uint64_t mClockValue {0};
        DynBuf mBuf {};

        [[nodiscard]] bool doRead();
        [[nodiscard]] static uint64_t clockValueInHz(uint64_t rawClockValue);
    };
}
#endif /* MALI_USERSPACE_MALIGPUCLOCKPOLLEDDRIVER_H_ */
