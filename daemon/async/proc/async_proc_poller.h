/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/continuations/async_initiate.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/FsEntry.h"
#include "lib/Utils.h"
#include "linux/proc/ProcessPollerBase.h"

#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/bind.hpp>
#include <boost/mp11/tuple.hpp>
#include <boost/system/error_code.hpp>

namespace async {

    namespace detail {
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

    /**
     * Scans the contents of /proc/[PID]/stat, /proc/[PID]/statm, /proc/[PID]/task/[TID]/stat and /proc/[PID]/task/[TID]/statm files
     * passing the extracted records into the IProcessPollerReceiver interface
     *
     * @tparam Executor Executor type
     */
    template<typename Executor>
    class async_proc_poller_t : public std::enable_shared_from_this<async_proc_poller_t<Executor>> {
    public:
        /* Callback signatures. */
        using on_process_directory_type = std::function<void(int, const lib::FsEntry &)>;
        using on_thread_directory_type = std::function<void(int, int, const lib::FsEntry &)>;
        using on_thread_details_type = std::function<void(int,
                                                          int,
                                                          const lnx::ProcPidStatFileRecord &,
                                                          const std::optional<lnx::ProcPidStatmFileRecord> &,
                                                          const std::optional<lib::FsEntry> &)>;

        explicit async_proc_poller_t(Executor & executor) : executor {executor}, procDir {lib::FsEntry::create("/proc")}
        {
        }

        /**
         * Asynchronously reads all the PIDs under /proc and optionally all the sub threads with stat and statm files,
         * then calls the appropriate @a callbacks for each file or directory for all threads and all PIDs.
         *
         * @tparam CompletionToken Completion handler type.
         * @tparam Callbacks Up to 3 unique callback types that must match the signatures in the above typedefs
         * @param token A callable called on the completion of the async operation.
         * passed an error code of type boost::system::error_code.
         * @param callbacks Callback instances
         */
        template<typename CompletionToken, typename... Callbacks>
        auto async_poll(CompletionToken && token, Callbacks &&... callbacks)
        {
            static_assert(sizeof...(Callbacks) > 0, "At least one callback must be provided");
            static_assert(sizeof...(Callbacks) <= 3, "Too many callbacks provided");

            using namespace async::continuations;

            auto callbacks_wrapper = callbacks_t {};

            constexpr auto want_threads = boost::mp11::mp_any_of_q<
                std::tuple<Callbacks...>,
                boost::mp11::mp_bind<std::is_convertible, boost::mp11::_1, on_thread_directory_type>>::value;
            constexpr auto want_stats = boost::mp11::mp_any_of_q<
                std::tuple<Callbacks...>,
                boost::mp11::mp_bind<std::is_convertible, boost::mp11::_1, on_thread_details_type>>::value;

            // Iterate over the callbacks and assign to the right var
            boost::mp11::tuple_for_each(std::tuple {std::forward<Callbacks>(callbacks)...}, [&](auto && callback) {
                using callback_type = std::decay_t<decltype(callback)>;

                if constexpr (std::is_convertible_v<callback_type, on_process_directory_type>) {
                    runtime_assert(!callbacks_wrapper.on_process_directory, "Callbacks must be unique");
                    callbacks_wrapper.on_process_directory = std::move(callback);
                }
                else if constexpr (std::is_convertible_v<callback_type, on_thread_directory_type>) {
                    runtime_assert(!callbacks_wrapper.on_thread_directory, "Callbacks must be unique");
                    callbacks_wrapper.on_thread_directory = std::move(callback);
                }
                else if constexpr (std::is_convertible_v<callback_type, on_thread_details_type>) {
                    runtime_assert(!callbacks_wrapper.on_thread_details, "Callbacks must be unique");
                    callbacks_wrapper.on_thread_details = std::move(callback);
                }
                else {
                    static_assert(lib::always_false<callback_type>::value, "Unhandled callback signature");
                }
            });

            // Default initialise any unassigned callbacks so we can lose a few branches later
            {
                const auto callback_ptrs = std::tuple {&callbacks_wrapper.on_process_directory,
                                                       &callbacks_wrapper.on_thread_directory,
                                                       &callbacks_wrapper.on_thread_details};
                boost::mp11::tuple_for_each(callback_ptrs, [&](auto callback_ptr) {
                    auto & callback = *callback_ptr;
                    if (!callback) {
                        callback = [](auto &&...) {};
                    }
                });
            }

            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = this->shared_from_this(), callbacks = std::move(callbacks_wrapper)]() mutable {
                    return start_on(self->executor) | //
                           then([self, callbacks = std::move(callbacks)]() mutable {
                               auto iterator = lib::FsEntryDirectoryIterator(self->procDir.children());
                               while (std::optional<lib::FsEntry> entry = iterator.next()) {
                                   if (async::detail::is_pid_directory(*entry)) {
                                       self->template process_pid_directory<want_threads, want_stats>(*entry,
                                                                                                      callbacks);
                                   }
                               }

                               // Currently we always succeed
                               return boost::system::error_code {};
                           });
                },
                token);
        }

    private:
        struct callbacks_t {
            on_process_directory_type on_process_directory;
            on_thread_directory_type on_thread_directory;
            on_thread_details_type on_thread_details;
        };

        template<bool WantThreads, bool WantStats>
        static void process_pid_directory(const lib::FsEntry & entry, callbacks_t & callbacks)
        {
            const auto name = entry.name();
            auto exe_path = detail::get_process_exe_path(entry);

            // read the pid
            const auto pid = std::strtol(name.c_str(), nullptr, 0);

            // call the receiver object
            callbacks.on_process_directory(pid, entry);

            // process threads?
            if constexpr (WantThreads || WantStats) {
                // the /proc/[PID]/task directory
                const auto task_directory = lib::FsEntry::create(entry, "task");

                // the /proc/[PID]/task/[PID]/ directory
                const auto task_pid_directory = lib::FsEntry::create(task_directory, name);
                const auto task_pid_directory_stats = task_pid_directory.read_stats();

                // if for some reason taskPidDirectory does not exist, then use stat and statm in the procPid directory instead
                if ((!task_pid_directory_stats.exists())
                    || (task_pid_directory_stats.type() != lib::FsEntry::Type::DIR)) {
                    process_tid_directory<WantStats>(pid, entry, exe_path, callbacks);
                }

                // scan all the TIDs in the task directory
                auto task_iterator = task_directory.children();
                while (auto task_entry = task_iterator.next()) {
                    if (detail::is_pid_directory(*task_entry)) {
                        process_tid_directory<WantStats>(pid, *task_entry, exe_path, callbacks);
                    }
                }
            }
        }

        template<bool WantStats>
        static void process_tid_directory(int pid,
                                          const lib::FsEntry & entry,
                                          const std::optional<lib::FsEntry> & exe,
                                          callbacks_t & callbacks)
        {
            const long tid = std::strtol(entry.name().c_str(), nullptr, 0);

            // call the receiver object
            callbacks.on_thread_directory(pid, tid, entry);

            // process stats?
            if constexpr (WantStats) {
                std::optional<lnx::ProcPidStatmFileRecord> statm_file_record {lnx::ProcPidStatmFileRecord()};

                // open /proc/[PID]/statm
                {
                    const lib::FsEntry statm_file = lib::FsEntry::create(entry, "statm");
                    const lib::FsEntry::Stats statm_file_stats = statm_file.read_stats();

                    if (statm_file_stats.exists() && statm_file_stats.type() == lib::FsEntry::Type::FILE) {
                        const std::string statm_file_contents = lib::readFileContents(statm_file);

                        if (!lnx::ProcPidStatmFileRecord::parseStatmFile(*statm_file_record,
                                                                         statm_file_contents.c_str())) {
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
                        lnx::ProcPidStatFileRecord stat_file_record;
                        if (lnx::ProcPidStatFileRecord::parseStatFile(stat_file_record, stat_file_contents.c_str())) {
                            callbacks.on_thread_details(pid, tid, stat_file_record, statm_file_record, exe);
                        }
                    }
                }
            }
        }

        Executor & executor;
        lib::FsEntry procDir;
    };
}
