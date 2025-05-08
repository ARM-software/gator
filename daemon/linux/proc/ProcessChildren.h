/* Copyright (C) 2018-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCESS_CHILDREN_H
#define INCLUDE_LINUX_PROC_PROCESS_CHILDREN_H

#include "lib/Syscall.h"

#include <csignal>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include <unistd.h>

namespace lnx {

    enum class tid_enumeration_mode_t : std::uint8_t {
        /** Only collect the pids specified by the user */
        self_only = 0,
        /** Only collect the pids specified by the user and any of their threads */
        self_and_threads = 1,
        /** Only collect the pids specified by the user and any of their threads, and any other processes they spawn */
        self_and_threads_and_children = 2,
    };

    /**
     * Inherently racey function to collect child tids because threads can be created and destroyed while this is running
     */
    void addTidsRecursively(std::set<int> & tids, int tid, tid_enumeration_mode_t tid_enumeration_mode);

    /**
     * Inherently racey function to collect child tids because threads can be created and destroyed while this is running
     *
     * @return as many of the known child tids (including child processes)
     */
    inline std::set<int> getChildTids(int tid, tid_enumeration_mode_t tid_enumeration_mode)
    {
        std::set<int> result;
        addTidsRecursively(result, tid, tid_enumeration_mode);
        return result;
    }

    /** RAII object that sends SIGCONT to some pid on request or dtor */
    class sig_continuer_t {
    public:
        constexpr sig_continuer_t() = default;
        explicit constexpr sig_continuer_t(pid_t pid) : pid(pid) {}

        // not copyable
        sig_continuer_t(sig_continuer_t const &) = delete;
        sig_continuer_t & operator=(sig_continuer_t const &) = delete;

        // only movable
        sig_continuer_t(sig_continuer_t && that) noexcept : pid(std::exchange(that.pid, 0)) {}
        sig_continuer_t & operator=(sig_continuer_t && that) noexcept
        {
            if (this != &that) {
                sig_continuer_t tmp {std::move(that)};
                std::swap(pid, tmp.pid);
            }
            return *this;
        }

        // destructor sends sigcont
        ~sig_continuer_t() noexcept { signal(); }

        /** sent sigcont to the target pid */
        void signal() noexcept
        {
            pid_t pid {std::exchange(this->pid, 0)};
            if (pid != 0) {
                lib::kill(pid, SIGCONT);
            }
        }

    private:
        pid_t pid {0};
    };

    /** Find all the tids associated with a set of pids and sigstop them (so long as the pid is not in the filter set) */
    [[nodiscard]] std::set<pid_t> stop_all_tids(std::set<pid_t> const & pids,
                                                std::set<pid_t> const & filter_set,
                                                std::map<pid_t, sig_continuer_t> & paused_tids,
                                                tid_enumeration_mode_t tid_enumeration_mode);
}

#endif
