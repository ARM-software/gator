/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#pragma once

#include <istream>
#include <memory>
#include <string>

#include <boost/filesystem/path.hpp>

namespace logging {

    class log_file_t {
    public:
        log_file_t() = default;

        log_file_t(boost::filesystem::path file) : file(std::move(file)) {}

        [[nodiscard]] std::unique_ptr<std::istream> open_for_reading() const;

        void copy_to(const std::string & dir);

        [[nodiscard]] bool valid() const;

    private:
        boost::filesystem::path file;
    };

    /** Interface that provides access to parts of the logging subsystem so that, for example, error
     * messages can be retreived and recorded in the counter XML files.
     */
    class log_access_ops_t {
    public:
        virtual ~log_access_ops_t() = default;

        /** Access the last sent error log item */
        [[nodiscard]] virtual std::string get_last_log_error() const = 0;

        /** Access the acumulation of all setup messages */
        [[nodiscard]] virtual std::string get_log_setup_messages() const = 0;

        /**
         * Instructs the logger to finish writing to the log file and make it available for reading.
         * If file logging was not enabled then the returned log_file_t instance may be invalid (check the
         * valid() method before using).
         */
        [[nodiscard]] virtual log_file_t capture_log_file() const = 0;

        /**
         * Truncates the log file and re-opens for writing.
         */
        virtual void restart_log_file() = 0;
    };
}
