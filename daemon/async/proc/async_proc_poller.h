/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#pragma once

#include "async/asio_traits.h"
#include "async/continuations/async_initiate.h"
#include "async/continuations/continuation.h"
#include "async/continuations/operations.h"
#include "async/continuations/use_continuation.h"
#include "lib/FsEntry.h"
#include "lib/Utils.h"
#include "linux/proc/ProcessPollerBase.h"

#include <memory>

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
        inline bool is_pid_directory(const lib::FsEntry & entry)
        {
            return lnx::isPidDirectory(entry);
        }

        /** @return The process exe path (or some estimation of it). Empty if the thread is a kernel thread, otherwise
         * contains 'something'
         */
        inline std::optional<std::string> get_process_exe_path(const lib::FsEntry & entry)
        {
            return lnx::getProcessExePath(entry);
        }

        /** Helper for iterating some directory asynchronously */
        template<typename Executor, typename Op>
        class async_dir_iterator_t : public std::enable_shared_from_this<async_dir_iterator_t<Executor, Op>> {
        public:
            async_dir_iterator_t(Executor const & executor, lib::FsEntry const & dir, Op && op)
                : executor(executor), dir(dir), iterator(dir.children()), op(std::forward<Op>(op))
            {
            }

            [[nodiscard]] async::continuations::polymorphic_continuation_t<boost::system::error_code> async_run()
            {
                using namespace async::continuations;

                LOG_TRACE("SCAN DIR: %s", dir.path().c_str());

                auto self = this->shared_from_this();

                return start_with(iterator.next(), boost::system::error_code {}) //
                     | loop(
                           // iterate while no error and has another value
                           [self](std::optional<lib::FsEntry> entry, boost::system::error_code const & ec) {
                               auto const valid = entry.has_value() && !ec;
                               LOG_TRACE("LOOP DIR: '%s' = '%s' == %d",
                                         self->dir.path().c_str(),
                                         (entry ? entry->path().c_str() : ""),
                                         valid);
                               return start_with(valid, std::move(entry), ec);
                           },
                           [self](std::optional<lib::FsEntry> entry, boost::system::error_code const & /*ec*/) {
                               LOG_TRACE("EXEC DIR: '%s' = '%s'", self->dir.path().c_str(), entry->path().c_str());
                               return start_on(self->executor)    //
                                    | self->op(std::move(*entry)) //
                                    | post_on(self->executor)     //
                                    | then([self](boost::system::error_code const & ec) {
                                          LOG_TRACE("... ec=%s", ec.message().c_str());
                                          return start_with(self->iterator.next(), ec);
                                      });
                           }) //
                     | then(
                           [self](std::optional<lib::FsEntry> const & /*entry*/, boost::system::error_code const & ec) {
                               LOG_TRACE("FINISHED DIR: '%s' = %s", self->dir.path().c_str(), ec.message().c_str());
                               return ec;
                           });
            }

        private:
            Executor executor;
            lib::FsEntry dir;
            lib::FsEntryDirectoryIterator iterator;
            Op op;
        };

        template<typename Executor, typename Op>
        auto make_async_dir_iterator(Executor && executor, lib::FsEntry const & dir, Op && op)
        {
            return std::make_shared<async_dir_iterator_t<Executor, Op>>(std::forward<Executor>(executor),
                                                                        dir,
                                                                        std::forward<Op>(op));
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
        using error_code_continuation_t = async::continuations::polymorphic_continuation_t<boost::system::error_code>;

        /* Callback signatures. */
        using on_process_directory_type = std::function<error_code_continuation_t(int, const lib::FsEntry &)>;
        using on_thread_directory_type = std::function<error_code_continuation_t(int, int, const lib::FsEntry &)>;
        using on_thread_details_type =
            std::function<error_code_continuation_t(int,
                                                    int,
                                                    const lnx::ProcPidStatFileRecord &,
                                                    const std::optional<lnx::ProcPidStatmFileRecord> &,
                                                    const std::optional<std::string> &)>;

        explicit async_proc_poller_t(Executor const & executor)
            : executor {executor}, procDir {lib::FsEntry::create("/proc")}
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
            boost::mp11::tuple_for_each(std::tuple {std::forward<Callbacks>(callbacks)...}, [&](auto callback) {
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
                        callback = [](auto &&...) {
                            return error_code_continuation_t {
                                start_with(boost::system::error_code {}),
                            };
                        };
                    }
                });
            }

            return async_initiate<continuation_of_t<boost::system::error_code>>(
                [self = this->shared_from_this(),
                 callbacks = std::make_shared<callbacks_t>(std::move(callbacks_wrapper))]() mutable {
                    auto iterator = async::detail::make_async_dir_iterator(
                        self->executor,
                        self->procDir,
                        [self, callbacks](lib::FsEntry entry) {
                            return async_proc_poller_t::template process_pid_directory<want_threads, want_stats>(
                                self,
                                std::move(entry),
                                callbacks);
                        });

                    return start_on(self->executor) //
                         | iterator->async_run();
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
        static error_code_continuation_t process_pid_directory(std::shared_ptr<async_proc_poller_t> self,
                                                               lib::FsEntry entry,
                                                               std::shared_ptr<callbacks_t> callbacks)
        {
            using namespace async::continuations;

            // ignore non-pid directories
            if (!async::detail::is_pid_directory(entry)) {
                return start_with(boost::system::error_code {});
            }

            auto name = entry.name();
            auto exe_path = async::detail::get_process_exe_path(entry);
            // read the pid
            auto pid = std::strtol(name.c_str(), nullptr, 0);

            // process threads?
            if constexpr (WantThreads || WantStats) {
                // call the receiver object
                return callbacks->on_process_directory(pid, entry)
                     // then process the threads
                     | then([entry = std::move(entry),
                             name = std::move(name),
                             exe_path = std::move(exe_path),
                             pid,
                             self = std::move(self),
                             callbacks](boost::system::error_code const & ec) mutable -> error_code_continuation_t {
                           // forward error?
                           if (ec) {
                               return start_with(ec);
                           }

                           // the /proc/[PID]/task directory
                           const auto task_directory = lib::FsEntry::create(entry, "task");

                           // the /proc/[PID]/task/[PID]/ directory
                           const auto task_pid_directory = lib::FsEntry::create(task_directory, name);
                           const auto task_pid_directory_stats = task_pid_directory.read_stats();

                           // if for some reason taskPidDirectory does not exist, then use stat and statm in the procPid directory instead
                           if ((!task_pid_directory_stats.exists())
                               || (task_pid_directory_stats.type() != lib::FsEntry::Type::DIR)) {
                               return process_tid_directory<WantStats>(pid,
                                                                       std::move(entry),
                                                                       std::move(exe_path),
                                                                       std::move(callbacks));
                           }

                           // scan all the TIDs in the task directory
                           auto task_iterator = async::detail::make_async_dir_iterator(
                               self->executor,
                               task_directory,
                               [exe_path, callbacks, pid](lib::FsEntry entry) {
                                   return async_proc_poller_t::template process_tid_directory<WantStats>(
                                       pid,
                                       std::move(entry),
                                       exe_path,
                                       callbacks);
                               });

                           return task_iterator->async_run();
                       });
            }
            else {
                // just call the receiver object
                return callbacks->on_process_directory(pid, entry);
            }
        }

        template<bool WantStats>
        static error_code_continuation_t process_tid_directory(int pid,
                                                               lib::FsEntry entry,
                                                               std::optional<std::string> exe,
                                                               std::shared_ptr<callbacks_t> callbacks)
        {
            using namespace async::continuations;

            const long tid = std::strtol(entry.name().c_str(), nullptr, 0);

            // process stats?
            if constexpr (WantStats) {
                // call the receiver object
                return callbacks->on_thread_directory(pid, tid, entry)
                     // then call the stats handler
                     | then([entry, callbacks, pid, tid, exe = std::move(exe)](
                                boost::system::error_code const & ec) mutable -> error_code_continuation_t {
                           // forward error?
                           if (ec) {
                               return start_with(ec);
                           }

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
                                   if (lnx::ProcPidStatFileRecord::parseStatFile(stat_file_record,
                                                                                 stat_file_contents.c_str())) {
                                       return callbacks->on_thread_details(pid,
                                                                           tid,
                                                                           stat_file_record,
                                                                           statm_file_record,
                                                                           exe);
                                   }
                               }
                           }

                           return start_with(boost::system::error_code {});
                       });
            }
            else {
                // call the receiver object
                return callbacks->on_thread_directory(pid, tid, entry);
            }
        }

        Executor executor;
        lib::FsEntry procDir;
    };

    template<typename Executor, std::enable_if_t<is_asio_executor_v<Executor>, bool> = false>
    auto make_async_proc_poller(Executor const & ex)
    {
        return std::make_shared<async_proc_poller_t<Executor>>(ex);
    }

    template<typename ExecutionContext, std::enable_if_t<is_asio_execution_context_v<ExecutionContext>, bool> = false>
    auto make_async_proc_poller(ExecutionContext & context)
    {
        return make_async_proc_poller(context.get_executor());
    }
}
