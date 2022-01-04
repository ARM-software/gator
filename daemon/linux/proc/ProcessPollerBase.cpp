/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcessPollerBase.h"

#include "lib/Format.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace lnx {
    namespace {
        /**
         * Checks the name of the FsEntry to see if it is a number, and checks the type
         * to see if it is a directory.
         *
         * @return True if criteria are met, false otherwise
         */
        bool isPidDirectory(const lib::FsEntry & entry)
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

        /**
         * Get the exe path for a process by reading /proc/[PID]/cmdline
         */
        std::optional<lib::FsEntry> getProcessCmdlineExePath(const lib::FsEntry & entry)
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

    std::optional<lib::FsEntry> getProcessExePath(const lib::FsEntry & entry)
    {
        auto proc_pid_exe = lib::FsEntry::create(entry, "exe");

        {
            auto exe_realpath = proc_pid_exe.realpath();

            if (exe_realpath) {
                // check android paths
                auto name = exe_realpath->name();
                if ((name == "app_process") || (name == "app_process32") || (name == "app_process64")) {
                    // use the command line instead
                    auto cmdline_exe = getProcessCmdlineExePath(entry);
                    if (cmdline_exe) {
                        return cmdline_exe;
                    }
                }

                // use realpath(/proc/pid/exe)
                return exe_realpath;
            }
        }

        // exe was linked to nothing, try getting from cmdline (but it must be for a real file)
        auto cmdline_exe = getProcessCmdlineExePath(entry);
        if (!cmdline_exe) {
            // no cmdline, must be a kernel thread
            return {};
        }

        // resolve the cmdline string to a real path
        if (cmdline_exe->path().front() == '/') {
            // already an absolute path, so just resolve it to its realpath
            auto cmldine_exe_realpath = cmdline_exe->realpath();
            if (cmldine_exe_realpath) {
                return cmldine_exe_realpath;
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

    void ProcessPollerBase::IProcessPollerReceiver::onProcessDirectory(int /*unused*/, const lib::FsEntry & /*unused*/)
    {
    }

    void ProcessPollerBase::IProcessPollerReceiver::onThreadDirectory(int /*unused*/,
                                                                      int /*unused*/,
                                                                      const lib::FsEntry & /*unused*/)
    {
    }

    void ProcessPollerBase::IProcessPollerReceiver::onThreadDetails(
        int /*unused*/,
        int /*unused*/,
        const ProcPidStatFileRecord & /*unused*/,
        const std::optional<ProcPidStatmFileRecord> & /*unused*/,
        const std::optional<lib::FsEntry> & /*unused*/)
    {
    }

    ProcessPollerBase::ProcessPollerBase() : procDir(lib::FsEntry::create("/proc")) {}

    void ProcessPollerBase::poll(bool wantThreads, bool wantStats, IProcessPollerReceiver & receiver)
    {
        // scan directory /proc for all pid files
        lib::FsEntryDirectoryIterator iterator = procDir.children();

        while (std::optional<lib::FsEntry> entry = iterator.next()) {
            if (isPidDirectory(*entry)) {
                processPidDirectory(wantThreads, wantStats, receiver, *entry);
            }
        }
    }

    void ProcessPollerBase::processPidDirectory(bool wantThreads,
                                                bool wantStats,
                                                IProcessPollerReceiver & receiver,
                                                const lib::FsEntry & entry)
    {
        const std::string name = entry.name();
        std::optional<lib::FsEntry> exe_path = getProcessExePath(entry);

        // read the pid
        const long pid = std::strtol(name.c_str(), nullptr, 0);

        // call the receiver object
        receiver.onProcessDirectory(pid, entry);

        // process threads?
        if (wantThreads || wantStats) {
            // the /proc/[PID]/task directory
            const lib::FsEntry task_directory = lib::FsEntry::create(entry, "task");

            // the /proc/[PID]/task/[PID]/ directory
            const lib::FsEntry task_pid_directory = lib::FsEntry::create(task_directory, name);
            const lib::FsEntry::Stats task_pid_directory_stats = task_pid_directory.read_stats();

            // if for some reason taskPidDirectory does not exist, then use stat and statm in the procPid directory instead
            if ((!task_pid_directory_stats.exists()) || (task_pid_directory_stats.type() != lib::FsEntry::Type::DIR)) {
                processTidDirectory(wantStats, receiver, pid, entry, exe_path);
            }

            // scan all the TIDs in the task directory
            lib::FsEntryDirectoryIterator task_iterator = task_directory.children();

            while (std::optional<lib::FsEntry> task_entry = task_iterator.next()) {
                if (isPidDirectory(*task_entry)) {
                    processTidDirectory(wantStats, receiver, pid, *task_entry, exe_path);
                }
            }
        }
    }

    void ProcessPollerBase::processTidDirectory(bool wantStats,
                                                IProcessPollerReceiver & receiver,
                                                const int pid,
                                                const lib::FsEntry & entry,
                                                const std::optional<lib::FsEntry> & exe)
    {
        const long tid = std::strtol(entry.name().c_str(), nullptr, 0);

        // call the receiver object
        receiver.onThreadDirectory(pid, tid, entry);

        // process stats?
        if (wantStats) {
            std::optional<ProcPidStatmFileRecord> statm_file_record {ProcPidStatmFileRecord()};

            // open /proc/[PID]/statm
            {
                const lib::FsEntry statm_file = lib::FsEntry::create(entry, "statm");
                const lib::FsEntry::Stats statm_file_stats = statm_file.read_stats();

                if (statm_file_stats.exists() && statm_file_stats.type() == lib::FsEntry::Type::FILE) {
                    const std::string statm_file_contents = lib::readFileContents(statm_file);

                    if (!ProcPidStatmFileRecord::parseStatmFile(*statm_file_record, statm_file_contents.c_str())) {
                        statm_file_record.reset();
                    }
                }
            }

            // open /proc/[PID]/stat
            {
                const lib::FsEntry stat_file = lib::FsEntry::create(entry, "stat");
                const lib::FsEntry::Stats stat_file_stats = stat_file.read_stats();

                if (stat_file_stats.exists() && stat_file_stats.type() == lib::FsEntry::Type::FILE) {
                    const std::string stat_file_contents = lib::readFileContents(stat_file);
                    ProcPidStatFileRecord stat_file_record;
                    if (ProcPidStatFileRecord::parseStatFile(stat_file_record, stat_file_contents.c_str())) {
                        receiver.onThreadDetails(pid, tid, stat_file_record, statm_file_record, exe);
                    }
                }
            }
        }
    }
}
