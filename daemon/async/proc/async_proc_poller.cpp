/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "async/proc/async_proc_poller.h"

namespace async::detail {
    namespace {

        /**
         * Get the exe path for a process by reading /proc/[PID]/cmdline
         */
        std::optional<lib::FsEntry> get_process_cmdline_exe_path(const lib::FsEntry & entry)
        {
            const lib::FsEntry cmdline_file = lib::FsEntry::create(entry, "cmdline");
            const std::string cmdline_contents = lib::readFileContents(cmdline_file);
            // need to extract just the first part of cmdline_contents (as it is an packed sequence of c-strings)
            // so use .c_str() to extract the first string (which is the exe path) and create a new string from it
            const std::string cmdline_exe = cmdline_contents.c_str(); // NOLINT(readability-redundant-string-cstr)
            if ((!cmdline_exe.empty()) && (cmdline_exe.at(0) != '\n') && (cmdline_exe.at(0) != '\r')) {
                return lib::FsEntry::create(cmdline_exe);
            }
            return std::optional<lib::FsEntry> {};
        }

    }

    /**
         * Checks the name of the FsEntry to see if it is a number, and checks the type
         * to see if it is a directory.
         *
         * @return True if criteria are met, false otherwise
         */
    bool is_pid_directory(const lib::FsEntry & entry)
    {
        // type must be directory
        const lib::FsEntry::Stats stats = entry.read_stats();
        if (stats.type() != lib::FsEntry::Type::DIR) {
            return false;
        }

        // name must be only digits
        const std::string name = entry.name();
        for (char chr : name) {
            if (std::isdigit(chr) == 0) {
                return false;
            }
        }

        return true;
    }

    /** @return The process exe path (or some estimation of it). Empty if the thread is a kernel thread, otherwise
         * contains 'something'
         */
    std::optional<lib::FsEntry> get_process_exe_path(const lib::FsEntry & entry)
    {
        auto proc_pid_exe = lib::FsEntry::create(entry, "exe");

        {
            auto exe_realpath = proc_pid_exe.realpath();

            if (exe_realpath) {
                // check android paths
                auto name = exe_realpath->name();
                if ((name == "app_process") || (name == "app_process32") || (name == "app_process64")) {
                    // use the command line instead
                    auto cmdline_exe = get_process_cmdline_exe_path(entry);
                    if (cmdline_exe) {
                        return cmdline_exe;
                    }
                }

                // use realpath(/proc/pid/exe)
                return exe_realpath;
            }
        }

        // exe was linked to nothing, try getting from cmdline (but it must be for a real file)
        auto cmdline_exe = get_process_cmdline_exe_path(entry);
        if (!cmdline_exe) {
            // no cmdline, must be a kernel thread
            return {};
        }

        // resolve the cmdline string to a real path
        if (cmdline_exe->path().front() == '/') {
            // already an absolute path, so just resolve it to its realpath
            auto cmdline_exe_realpath = cmdline_exe->realpath();
            if (cmdline_exe_realpath) {
                return cmdline_exe_realpath;
            }
        }
        else {
            // try relative to process cwd first
            auto cwd_file = lib::FsEntry::create(entry, "cwd");
            auto rel_exe_file = lib::FsEntry::create(cwd_file, cmdline_exe->path());
            auto abs_exe_file = rel_exe_file.realpath();

            if (abs_exe_file) {
                // great, use that
                return abs_exe_file;
            }
        }

        // we could not resolve exe or the command to a real path.
        // Since the exe_path value *must* contain something for any non-kernel PID,
        // then prefer to send 'comm' (so long as it is not an empty string)
        auto comm_file = lib::FsEntry::create(entry, "comm");
        auto comm_file_contents = lib::readFileContents(comm_file);
        if (!comm_file_contents.empty()) {
            return lib::FsEntry::create(comm_file_contents);
        }

        // comm was empty, so fall back to whatever the commandline was
        if (cmdline_exe) {
            return cmdline_exe;
        }

        // worst case just send /proc/<pid>/exe
        return proc_pid_exe;
    }
}
