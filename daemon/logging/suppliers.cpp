/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "logging/suppliers.h"

#include "Logging.h"

#include <fstream>

#include <boost/filesystem.hpp>

namespace logging {

    namespace fs = boost::filesystem;

    constexpr auto captured_log_file_name = "gator-log.txt";

    std::unique_ptr<std::istream> log_file_t::open_for_reading() const
    {
        return std::make_unique<std::ifstream>(file.string());
    }

    void log_file_t::copy_to(const std::string & dir)
    {
        auto dir_path = fs::path(dir);

        try {
            if (!fs::exists(dir_path)) {
                LOG_ERROR("Not copying log file. Capture dir does not exist: %s", dir.c_str());
                return;
            }

            auto dest_file = dir_path / captured_log_file_name;
            if (!fs::copy_file(file, dest_file, fs::copy_options::overwrite_existing)) {
                LOG_ERROR("Failed to copy capture log file to %s", dest_file.c_str());
            }
        }
        catch (const fs::filesystem_error & err) {
            LOG_ERROR("Could not move gator log file into the capture dir: %s", err.what());
        }
    }

    bool log_file_t::valid() const
    {
        try {
            return fs::exists(file);
        }
        catch (const fs::filesystem_error & err) {
            LOG_ERROR("Error validating log file: %s", err.what());
        }
        return false;
    }
}
