/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "android/ThermalDriver.h"

#include "IBlockCounterFrameBuilder.h"
#include "Logging.h"
#include "SessionData.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <dlfcn.h>
#include <mxml.h>
#include <unistd.h>

using namespace std::string_view_literals;

namespace gator::android {

    enum AThermalStatus {};
    using AThermalManager = struct AThermalManager;
    using FnPtr_AThermal_acquireManager = AThermalManager * (*) ();
    using FnPtr_AThermal_getCurrentThermalStatus = AThermalStatus (*)(AThermalManager *);
    using FnPtr_AThermal_releaseManager = void (*)(AThermalManager *);

    /**
     * AThermalWrapper struct is used to house the Thermal library function pointers.
     * This library must be accessed dynamically, as it does not exist on some target devices.
     * Requires Android 11+
     */
    struct AThermalWrapper {
    public:
        explicit AThermalWrapper(void * lib_ptr)
        {
            fn_acquireManager =
                reinterpret_cast<FnPtr_AThermal_acquireManager>(dlsym(lib_ptr, "AThermal_acquireManager"));
            fn_getCurrentThermalStatus = reinterpret_cast<FnPtr_AThermal_getCurrentThermalStatus>(
                dlsym(lib_ptr, "AThermal_getCurrentThermalStatus"));
            fn_releaseManager =
                reinterpret_cast<FnPtr_AThermal_releaseManager>(dlsym(lib_ptr, "AThermal_releaseManager"));
        }

        [[nodiscard]] FnPtr_AThermal_acquireManager acquireManager() const { return fn_acquireManager; };
        [[nodiscard]] FnPtr_AThermal_getCurrentThermalStatus getCurrentThermalStatus() const
        {
            return fn_getCurrentThermalStatus;
        };
        [[nodiscard]] FnPtr_AThermal_releaseManager releaseManager() const { return fn_releaseManager; };

    private:
        FnPtr_AThermal_acquireManager fn_acquireManager;
        FnPtr_AThermal_getCurrentThermalStatus fn_getCurrentThermalStatus;
        FnPtr_AThermal_releaseManager fn_releaseManager;
    };

    /**
     * ThermalCounter class defines a configurable counter for displaying information about Thermal Status in Streamline
     */
    class ThermalCounter : public DriverCounter {
    public:
        ThermalCounter(DriverCounter * next, const char * name, void * lib_ptr)
            : DriverCounter(next, name), atw(lib_ptr)
        {
        }

        // Intentionally undefined
        ThermalCounter(const ThermalCounter &) = delete;
        ThermalCounter & operator=(const ThermalCounter &) = delete;
        ThermalCounter(ThermalCounter &&) = delete;
        ThermalCounter & operator=(ThermalCounter &&) = delete;

        void setCounterValues(mxml_node_t * node);
        int64_t read() override;

    private:
        AThermalWrapper atw;
        void findThermalLibrary();
    };

    /**
     * Sets the values used to display the counter in streamline
     * @param node Should be an event node in the Thermal Query Category node.
     */
    void ThermalCounter::setCounterValues(mxml_node_t * node)
    {
        constexpr std::array<std::array<char const *, 4>, 7> activities {{
            {"activity1", "None", "activity_color1", "0x2e7d32"},
            {"activity2", "Light", "activity_color2", "0x627a2b"},
            {"activity3", "Moderate", "activity_color3", "0x877424"},
            {"activity4", "Severe", "activity_color4", "0xa76c1c"},
            {"activity5", "Critical", "activity_color5", "0xc56014"},
            {"activity6", "Emergency", "activity_color6", "0xe24e0a"},
            {"activity7", "Shutdown", "activity_color7", "0xff2d00"},
        }};

        mxmlElementSetAttr(node, "counter", getName());
        mxmlElementSetAttr(node, "title", "Android Thermal Throttling");
        mxmlElementSetAttr(node, "name", "Throttling State");
        mxmlElementSetAttr(node, "display", "average");
        mxmlElementSetAttr(node, "class", "activity");
        mxmlElementSetAttr(node, "units", "");
        mxmlElementSetAttr(node, "average_selection", "yes");
        mxmlElementSetAttr(node, "series_composition", "stacked");
        mxmlElementSetAttr(node, "rendering_type", "bar");
        mxmlElementSetAttr(node, "proc", "no");
        mxmlElementSetAttr(node, "per_core", "no");
        mxmlElementSetAttr(node, "cores", "1");
        mxmlElementSetAttr(node, "description", "Counter for reading Thermal status");

        for (const auto & activitie : activities) {
            mxmlElementSetAttr(node, activitie[0], activitie[1]);
            mxmlElementSetAttr(node, activitie[2], activitie[3]);
        }
    }

    /**
     * Gets the current value of thermal status and returns.
     */
    int64_t ThermalCounter::read()
    {
        auto * mgr = atw.acquireManager()();
        auto status = atw.getCurrentThermalStatus()(mgr);

        // Map AThermalStatus to integer indices of the activities array in setCounterValues
        // For docs on AThermalStatus:
        //     https://developer.android.com/ndk/reference/group/thermal
        // ATHERMAL_STATUS_NONE and ATHERMAL_STATUS_ERROR are intended to map to
        // the zeroth entry in the array.
        return std::max<int>(0, status);
    }

    /**
     * Checks that the thermal library exists on current device.
     * Sets value of lib_ptr to the location of the library if found.
     */
    void ThermalDriver::findThermalLibrary()
    {
#if defined(ANDROID) || defined(__ANDROID__)
        constexpr auto all_possible_libandroid_paths =
            std::array {"/system/lib64/libandroid.so"sv, "/system/lib/libandroid.so"sv};

        for (auto path : all_possible_libandroid_paths) {
            // If the library can be found and opened...
            lib_ptr = dlopen(path.data(), RTLD_LOCAL | RTLD_LAZY);
            if (lib_ptr != nullptr) {
                // And it contains the right symbols...
                if (dlsym(lib_ptr, "AThermal_acquireManager") != nullptr) {
                    // Then we have the right one and can exit
                    return;
                }
                // Otherwise close the library and try the next one
                dlclose(lib_ptr);
                lib_ptr = nullptr;
            }
        }
#else
        LOG_DEBUG("ThermalDriver is not supported on this target");
        lib_ptr = nullptr;
#endif
    }

    ThermalDriver::ThermalDriver() : PolledDriver("Thermal")
    {
        findThermalLibrary();
    }
    /**
     *  Performs counter discovery. Checks for conditions and creates one or more counters if those conditions are met.
     */
    void ThermalDriver::readEvents(mxml_node_t * /*unused*/)
    {
        if (lib_ptr != nullptr) {
            setCounters(new ThermalCounter(getCounters(), "Android_ThermalState", lib_ptr));
        }
    }

    /**
     * Writes available counters to events.xml.
     */
    void ThermalDriver::writeEvents(mxml_node_t * root) const
    {
        root = mxmlNewElement(root, "category");
        mxmlElementSetAttr(root, "name", "Thermal Query");

        for (auto * counter = static_cast<ThermalCounter *>(getCounters()); counter != nullptr;
             counter = static_cast<ThermalCounter *>(counter->getNext())) {
            mxml_node_t * node = mxmlNewElement(root, "event");
            counter->setCounterValues(node);
        }
    }

}
