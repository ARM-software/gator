/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "Drivers.h"
#include "GatorCLIParser.h"
#include "logging/suppliers.h"

#include <array>
#include <functional>

namespace gator::capture {

    using GatorReadyCallback = std::function<void()>;

    int beginCaptureProcess(const ParserResult & result,
                            Drivers & drivers,
                            std::array<int, 2> signalPipe,
                            logging::last_log_error_supplier_t last_log_error_supplier,
                            logging::log_setup_supplier_t log_setup_supplier,
                            const gator::capture::GatorReadyCallback & gatorReady);

}
