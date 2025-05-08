/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcessChildren.h"

#include "Logging.h"
#include "lib/String.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <set>

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

namespace lnx {
    // NOLINTNEXTLINE(misc-no-recursion)
    void addTidsRecursively(std::set<int> & tids, int tid, tid_enumeration_mode_t tid_enumeration_mode)
    {
        constexpr std::size_t buffer_size = 64; // should be large enough for the proc path

        auto result = tids.insert(tid);
        if (!result.second) {
            return; // we've already added this and its children
        }

        lib::printf_str_t<buffer_size> filename {};

        // try to get all children (forked processes), available since Linux 3.5
        switch (tid_enumeration_mode) {
            case tid_enumeration_mode_t::self_and_threads_and_children: {
                filename.printf("/proc/%d/task/%d/children", tid, tid);
                std::ifstream children {filename, std::ios_base::in};
                if (children) {
                    int child;
                    while (children >> child) {
                        addTidsRecursively(tids, child, tid_enumeration_mode);
                    }
                }
                break;
            }

            case tid_enumeration_mode_t::self_only:
            case tid_enumeration_mode_t::self_and_threads:
            default: {
                // nothing to do
                break;
            }
        }

        // Now add all threads for the process
        // If 'children' is not found then new processes won't be counted on onlined cpu.
        // We could read /proc/[pid]/stat for every process and create a map in reverse
        // but that would likely be time consuming
        switch (tid_enumeration_mode) {
            case tid_enumeration_mode_t::self_and_threads:
            case tid_enumeration_mode_t::self_and_threads_and_children: {
                filename.printf("/proc/%d/task", tid);
                const std::unique_ptr<DIR, int (*)(DIR *)> taskDir {opendir(filename), &closedir};
                if (taskDir != nullptr) {
                    const dirent * taskEntry;
                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    while ((taskEntry = readdir(taskDir.get())) != nullptr) {
                        // no point recursing if we're relying on the fall back
                        if (std::strcmp(taskEntry->d_name, ".") != 0 && std::strcmp(taskEntry->d_name, "..") != 0) {
                            const auto child = std::strtol(taskEntry->d_name, nullptr, 10);
                            if (child > 0) {
                                tids.insert(pid_t(child));
                            }
                        }
                    }
                }
                break;
            }
            case tid_enumeration_mode_t::self_only:
            default: {
                break;
            }
        }
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::set<pid_t> stop_all_tids(std::set<pid_t> const & pids,
                                  std::set<pid_t> const & filter_set,
                                  std::map<pid_t, sig_continuer_t> & paused_tids,
                                  tid_enumeration_mode_t tid_enumeration_mode)
    {
        constexpr unsigned sleep_usecs = 100;

        std::set<pid_t> result {};
        bool modified {true};

        // repeat until no new items detected
        while (modified && !pids.empty()) {
            // clear modified for next iteration
            modified = false;

            // first find any children
            std::set<int> tids {};
            for (pid_t pid : pids) {
                addTidsRecursively(tids, pid, tid_enumeration_mode);
            }
            // then sigstop them all
            for (pid_t tid : tids) {
                // already stopped ?
                if (paused_tids.count(tid) > 0) {
                    // record it in the result as it is still a tracked pid
                    result.insert(tid);
                    // but no need to stop it again
                    continue;
                }

                // to be ignored ?
                if (filter_set.count(tid) > 0) {
                    // just skip it
                    continue;
                }

                // stop it?
                if (lib::kill(tid, SIGSTOP) == -1) {
                    // error
                    auto const error = errno;

                    // add it to the map with an empty entry so as not to poll it again, but dont set modified
                    LOG_WARNING("Could not SIGSTOP %d due to errno=%d", tid, error);
                    paused_tids.emplace(tid, sig_continuer_t {});

                    // add it to 'result' if exited
                    if (error != ESRCH) {
                        result.insert(tid);
                    }
                }
                else {
                    LOG_FINE("Successfully stopped %d", tid);
                    // success
                    paused_tids.emplace(tid, sig_continuer_t {tid});
                    result.insert(tid);
                    modified = true;
                }
            }

            // sleep some tiny amount of time so that the signals can propogate before checking again
            if (modified) {
                usleep(sleep_usecs);
            }
        }

        return result;
    }
}
