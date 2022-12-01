/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#include "agents/perfetto/perfetto_driver.h"

#include "Logging.h"
#include "SessionData.h"
#include "capture/Environment.h"
#include "k/perf_event.h"
#include "lib/perfetto_utils.h"

#include <vector>

namespace agents::perfetto {

    perfetto_driver_t::perfetto_driver_t(const char * maliFamilyName)
        : PolledDriver("MaliTimeline")
    {
        if(maliFamilyName != nullptr) {
            this->maliFamilyName = maliFamilyName;
        }
    }

    void perfetto_driver_t::setupCounter(Counter & counter) {
        counter.setExcludeFromCapturedXml();
        perfetto_requested = true;

        std::string error_message = get_error_message();
        if (error_message.empty()) {
            perfetto_enabled = true;
        } else {
            LOG_SETUP(error_message);
        }
    }

    void perfetto_driver_t::writeEvents(mxml_node_t * root) const
    {
        root = mxmlNewElement(root, "category");
        mxmlElementSetAttr(root, "name", "Mali Timeline");
        root = mxmlNewElement(root, "event");
        mxmlElementSetAttr(root, "counter", "MaliTimeline_Perfetto");
        mxmlElementSetAttr(root, "title", "Mali Timeline Events");
        mxmlElementSetAttr(root, "name", "Perfetto");
    }

    void perfetto_driver_t::readEvents(mxml_node_t * const /* unused */)
    {
        bool is_android = lib::is_android();
        bool traced_running = lib::check_traced_running();
        if (isMaliGpu() && is_android && traced_running) {
            setCounters(new DriverCounter(getCounters(), "MaliTimeline_Perfetto"));
        }
    }

    std::vector<std::string> perfetto_driver_t::get_other_warnings() const
    {
        std::vector<std::string> other_message;
        if(perfetto_requested) {
            other_message.emplace_back(get_error_message());
        }
        return other_message;
    }

    bool perfetto_driver_t::isMaliGpu() const {
        return !maliFamilyName.empty();
    }

    bool perfetto_driver_t::perfettoEnabled() const {
        return perfetto_enabled;
    }

    std::string perfetto_driver_t::get_error_message() const
    {
        if (!isMaliGpu()) {
            return std::string {"Mali Timeline view is not available on this device as it does not have a Mali GPU"};
        }

        bool not_on_android = !lib::is_android();
        if (not_on_android) {
            return std::string {"Mali Timeline view is not available on this device as it is not running Android."};
        }

        bool traced_not_running = !lib::check_traced_running();
        if (traced_not_running) {
            return std::string {"Mali Timeline view is not available on this device as perfetto is unavailable."};
        }


        return std::string {};
    }
}
