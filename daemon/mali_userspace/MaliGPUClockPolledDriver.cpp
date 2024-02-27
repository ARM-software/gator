/* Copyright (C) 2023-2024 by Arm Limited. All rights reserved. */

#include "MaliGPUClockPolledDriver.h"

#include "Logging.h"
#include "PolledDriver.h"
#include "mali_userspace/MaliGPUClockPolledDriverCounter.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <mxml.h>
#include <unistd.h>

namespace mali_userspace {

    MaliGPUClockPolledDriver::MaliGPUClockPolledDriver(std::string clockPath, unsigned deviceNumber)
        : PolledDriver("MaliGPUClock"), mClockPath(std::move(clockPath)), deviceNumber(deviceNumber)
    {
        counterName = ARM_MALI_CLOCK.data() + std::to_string(deviceNumber);
        LOG_DEBUG("GPU CLOCK POLLING '%s' for mali%d", mClockPath.c_str(), deviceNumber);
    }

    void MaliGPUClockPolledDriver::readEvents(mxml_node_t * const /*root*/)
    {
        if (access(mClockPath.c_str(), R_OK) == 0) {
            LOG_SETUP("Mali GPU counters\nAccess %s is OK. GPU frequency counters available.", mClockPath.c_str());
            setCounters(
                new mali_userspace::MaliGPUClockPolledDriverCounter(getCounters(), counterName.c_str(), mClockValue));
        }
        else {
            LOG_SETUP("Mali GPU counters\nCannot access %s. GPU frequency counters not available.", mClockPath.c_str());
        }
    }

    int MaliGPUClockPolledDriver::writeCounters(available_counter_consumer_t const & consumer) const
    {
        int count = 0;
        if (access(mClockPath.c_str(), R_OK) == 0) {
            consumer(counter_type_t::counter, counterName);
            count++;
        }
        else {
            LOG_ERROR("Mali GPU counters\nCannot access %s. GPU frequency counters not available.", mClockPath.c_str());
        }
        return count;
    }

    void MaliGPUClockPolledDriver::read(IBlockCounterFrameBuilder & buffer)
    {
        if (!doRead()) {
            LOG_ERROR("Unable to read GPU clock frequency for %s", mClockPath.c_str());
            handleException();
        }
        PolledDriver::read(buffer);
    }

    bool MaliGPUClockPolledDriver::doRead()
    {
        if (!countersEnabled()) {
            return true;
        }

        if (!mBuf.read(mClockPath.c_str())) {
            return false;
        }

        const uint64_t rawClockValue = strtoull(mBuf.getBuf(), nullptr, 0);
        mClockValue = clockValueInHz(rawClockValue);

        return true;
    }

    /**
     * We apply conversion to Hz under the assumption that the GPU clock cannot be
     * below 10MHz or above 10GHz.
     */
    uint64_t MaliGPUClockPolledDriver::clockValueInHz(const uint64_t rawClockValue)
    {
        const bool rawClockIsInMHz = (rawClockValue > 0 && rawClockValue < MIN_RAW_kHz);
        const bool rawClockIsInkHz = (rawClockValue < MAX_RAW_kHz);

        if (rawClockIsInMHz) {
            return rawClockValue * ONE_MILLION;
        }
        if (rawClockIsInkHz) {
            return rawClockValue * ONE_THOUSAND;
        }
        return rawClockValue;
    }

    void MaliGPUClockPolledDriver::writeEvents(mxml_node_t * root) const
    {
        mxml_node_t * node = mxmlNewElement(root, "category");
        mxmlElementSetAttr(node, "name", "Mali Misc");
        mxmlElementSetAttr(node, "per_cpu", "no");

        mxml_node_t * nodeEvent = mxmlNewElement(node, "event");
        mxmlElementSetAttr(nodeEvent, "counter", counterName.c_str());
        mxmlElementSetAttr(nodeEvent, "title", "Mali Clock");
        auto eventName = "Frequency (Device #" + std::to_string(deviceNumber) + ")";
        mxmlElementSetAttr(nodeEvent, "name", eventName.c_str());
        mxmlElementSetAttr(nodeEvent, "class", "absolute");
        mxmlElementSetAttr(nodeEvent, "rendering_type", "line");
        mxmlElementSetAttr(nodeEvent, "display", "maximum");
        mxmlElementSetAttr(nodeEvent, "description", "GPU clock frequency in Hz");
        mxmlElementSetAttr(nodeEvent, "units", "Hz");
    }
}
