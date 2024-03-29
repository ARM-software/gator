/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "logging/log_sink_t.h"

#include <iostream>

namespace logging {

    class std_log_sink_t : public log_sink_t {
    public:
        void write_log(std::string_view log_item) override { std::cerr << log_item << std::endl; }
    };
}
