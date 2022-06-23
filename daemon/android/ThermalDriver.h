/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */

#include "PolledDriver.h"

namespace gator::android {

    // forward declaration
    typedef struct _mxml_node_s mxml_node_t;

    /**
     * ThermalDriver class is used to send Thermal data back to streamline.
     */
    class ThermalDriver : public PolledDriver {
    public:
        ThermalDriver();

        // Intentionally unimplemented
        ThermalDriver(const ThermalDriver &) = delete;
        ThermalDriver & operator=(const ThermalDriver &) = delete;
        ThermalDriver(ThermalDriver &&) = delete;
        ThermalDriver & operator=(ThermalDriver &&) = delete;

        void readEvents(mxml_node_t * xml) override;
        void writeEvents(mxml_node_t * root) const override;

    private:
        void * lib_ptr; /**< Used to hold a pointer to the Thermal Library*/

        /**
         * Checks if the thermal library exists
         * Sets lib_ptr to library location if found, or nullptr if not.
         **/
        void findThermalLibrary();
    };
}
