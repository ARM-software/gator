/* Copyright (C) 2022 by Arm Limited. All rights reserved. */
#pragma once

#include "PolledDriver.h"

namespace agents::perfetto {

    class perfetto_driver_t : public PolledDriver {

    public:
        explicit perfetto_driver_t(const char * maliFamilyName);
        void writeEvents(mxml_node_t * root) const override;
        void readEvents(mxml_node_t * root) override;
        std::vector<std::string> get_other_warnings() const override;
        void setupCounter(Counter & counter) override;
        bool perfettoEnabled() const;

    private:
        std::string maliFamilyName;
        [[nodiscard]] bool isMaliGpu() const;
        [[nodiscard]] std::string get_error_message() const;
        bool perfetto_requested = false;
        bool perfetto_enabled = false;
    };

}
