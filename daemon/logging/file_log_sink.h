/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "logging/log_sink_t.h"

#include <array>
#include <cstring>
#include <fstream>
#include <ios>
#include <iostream>
#include <mutex>

#include <boost/filesystem.hpp>

namespace logging {

    /** A log sink implementation that stores log data to a file in the current working directory. */
    class file_log_sink_t : public log_sink_t {
    public:
        file_log_sink_t();

        void write_log(std::string_view log_item) override
        {
            const std::lock_guard<std::mutex> lock {file_mutex};
            log_file << log_item << '\n';
        }

        boost::filesystem::path get_log_file_path() const { return log_file_path; }

        /**
         * Re-opens and truncates the log file. No log rotation is performed.
         */
        void restart_log_file();

    private:
        std::mutex file_mutex;
        boost::filesystem::path log_file_path;
        std::ofstream log_file;

        void open_log_file();
    };
}
