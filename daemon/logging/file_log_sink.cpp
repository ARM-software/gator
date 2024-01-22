/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#include "file_log_sink.h"

#include "OlyUtility.h"

#include <array>
#include <cstddef>
#include <ios>
#include <mutex>
#include <string_view>

#include <boost/filesystem/path.hpp>

namespace {
    constexpr std::size_t path_buffer_size = 4096;

    constexpr std::string_view gator_log_file_name = "gator-log.txt";
}

namespace logging {

    file_log_sink_t::file_log_sink_t()
    {
        std::array<char, path_buffer_size> path_buffer {};

        if (getApplicationFullPath(path_buffer.data(), path_buffer.size()) != 0) {
            throw std::ios_base::failure(
                "Cannot determine the path of the gatord executable. Unable to create log file.");
        }

        const auto gator_dir = boost::filesystem::path(path_buffer.data());
        log_file_path = gator_dir / gator_log_file_name;
        open_log_file();
    }

    void file_log_sink_t::restart_log_file()
    {
        const std::lock_guard<std::mutex> lock {file_mutex};
        log_file.close();
        open_log_file();
    }

    void file_log_sink_t::open_log_file()
    {
        log_file.open(log_file_path.c_str(), std::ios_base::out | std::ios_base::trunc);
        if (log_file.fail() || !log_file.is_open()) {
            throw std::ios_base::failure("Can not open log file for writing. Is the working directory read-only?");
        }
    }
}
