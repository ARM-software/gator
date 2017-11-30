/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "lib/Format.h"
#include "linux/proc/ProcessPollerBase.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace lnx
{
    namespace
    {
        /**
         * Checks the name of the FsEntry to see if it is a number, and checks the type
         * to see if it is a directory.
         *
         * @return True if criteria are met, false otherwise
         */
        static bool isPidDirectory(const lib::FsEntry & entry)
        {
            // type must be directory
            const lib::FsEntry::Stats stats = entry.read_stats();
            if (stats.type() != lib::FsEntry::Type::DIR) {
                return false;
            }

            // name must be only digits
            const std::string name = entry.name();
            for (char chr : name) {
                if (!std::isdigit(chr)) {
                    return false;
                }
            }

            return true;
        }

        /**
         * Get the exe path for a process by reading /proc/[PID]/cmdline
         */
        static lib::Optional<lib::FsEntry> getProcessCmdlineExePath(const lib::FsEntry & entry)
        {
            const lib::FsEntry cmdline_file = lib::FsEntry::create(entry, "cmdline");
            const std::string cmdline_contents = lib::readFileContents(cmdline_file);
            // need to extract just the first part of cmdline_contents (as it is an packed sequence of c-strings)
            // so use .c_str() to extract the first string (which is the exe path) and create a new string from it
            const std::string cmdline_exe = cmdline_contents.c_str();
            if ((!cmdline_exe.empty()) && (cmdline_exe.at(0) != '\n') && (cmdline_exe.at(0) != '\r')) {
                return lib::FsEntry::create(cmdline_exe);
            }
            return lib::Optional<lib::FsEntry>{};
        }

        /**
         * Get the exe path for the process
         */
        static lib::Optional<lib::FsEntry> getProcessExePath(const lib::FsEntry & entry)
        {
            const lib::FsEntry exe_file = lib::FsEntry::create(entry, "exe");

            lib::Optional<lib::FsEntry> exe_path = exe_file.realpath();

            if (exe_path) {
                // check android paths
                const std::string name = exe_path->name();
                if ((name == "app_process") || (name == "app_process32") || (name == "app_process64")) {
                    // use the command line instead
                    const lib::Optional<lib::FsEntry> cmdline_exe = getProcessCmdlineExePath(entry);
                    if (cmdline_exe) {
                        return cmdline_exe;
                    }
                }

                // use realpath(exe)
                return exe_path;
            }
            else {
                // exe was linked to nothing, try getting from cmdline
                return getProcessCmdlineExePath(entry);
            }
        }
    }

    ProcessPollerBase::IProcessPollerReceiver::~IProcessPollerReceiver()
    {
    }

    void ProcessPollerBase::IProcessPollerReceiver::onProcessDirectory(int, const lib::FsEntry &)
    {
    }

    void ProcessPollerBase::IProcessPollerReceiver::onThreadDirectory(int, int, const lib::FsEntry &)
    {
    }

    void ProcessPollerBase::IProcessPollerReceiver::onThreadDetails(int, int, const ProcPidStatFileRecord &,
                                                                    const lib::Optional<ProcPidStatmFileRecord> &,
                                                                    const lib::Optional<lib::FsEntry> &)
    {
    }

    ProcessPollerBase::ProcessPollerBase()
            : procDir(lib::FsEntry::create("/proc"))
    {
    }

    ProcessPollerBase::~ProcessPollerBase()
    {
    }

    void ProcessPollerBase::poll(bool wantThreads, bool wantStats, IProcessPollerReceiver & receiver)
    {
        // scan directory /proc for all pid files
        lib::FsEntryDirectoryIterator iterator = procDir.children();

        while (lib::Optional<lib::FsEntry> entry = iterator.next()) {
            if (isPidDirectory(*entry)) {
                processPidDirectory(wantThreads, wantStats, receiver, *entry);
            }
        }
    }

    void ProcessPollerBase::processPidDirectory(bool wantThreads, bool wantStats, IProcessPollerReceiver & receiver,
                                                const lib::FsEntry & entry)
    {
        const std::string name = entry.name();
        lib::Optional<lib::FsEntry> exe_path = getProcessExePath(entry);

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

            while (lib::Optional<lib::FsEntry> task_entry = task_iterator.next()) {
                if (isPidDirectory(*task_entry)) {
                    processTidDirectory(wantStats, receiver, pid, *task_entry, exe_path);
                }
            }
        }
    }

    void ProcessPollerBase::processTidDirectory(bool wantStats, IProcessPollerReceiver & receiver, const int pid,
                                                const lib::FsEntry & entry, const lib::Optional<lib::FsEntry> & exe)
    {
        const long tid = std::strtol(entry.name().c_str(), nullptr, 0);

        // call the receiver object
        receiver.onThreadDirectory(pid, tid, entry);

        // process stats?
        if (wantStats) {
            lib::Optional<ProcPidStatmFileRecord> statm_file_record { ProcPidStatmFileRecord() };

            // open /proc/[PID]/statm
            {
                const lib::FsEntry statm_file = lib::FsEntry::create(entry, "statm");
                const lib::FsEntry::Stats statm_file_stats = statm_file.read_stats();

                if (statm_file_stats.exists() && statm_file_stats.type() == lib::FsEntry::Type::FILE) {
                    const std::string statm_file_contents = lib::readFileContents(statm_file);

                    if (!ProcPidStatmFileRecord::parseStatmFile(*statm_file_record, statm_file_contents.c_str())) {
                        statm_file_record.clear();
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
