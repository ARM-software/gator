/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#pragma once

#include "Drivers.h"
#include "GatorCLIParser.h"
#include "logging/suppliers.h"

#include <array>
#include <functional>

namespace capture {

    /**
     * @brief A callback interface that should be implemented by parties wishing to be informed
     * of siginficant events from the agent during the capture process.
     *
     * Note: this code is here as a stop-gap measure to enable rudimentary communcation between
     * gator-child and the shell process. It is expected that this will be replaced by a more
     * appropriate IPC implementation as gator-child is replaced by Asio agents.
     */
    class capture_process_event_listener_t {
    public:
        virtual ~capture_process_event_listener_t() = default;

        /**
         * @brief Called by the capturing agent to signal to the parent that has it started successfully
         * and is ready to receive connections (e.g. from Streamline).
         */
        virtual void process_initialised() = 0;

        /**
         * @brief Called by the capturing agent when it has performed any required initialisation
         * (e.g. enumerating & configuring counters) and it is ready for the target application to
         * be started.
         * @return true unless the android package or other target could not be started
         */
        [[nodiscard]] virtual bool waiting_for_target() = 0;
    };

    using GatorReadyCallback = std::function<void()>;

    int beginCaptureProcess(const ParserResult & result,
                            Drivers & drivers,
                            std::array<int, 2> signalPipe,
                            logging::log_access_ops_t & log_ops,
                            capture_process_event_listener_t & event_listener);
}
