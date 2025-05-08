/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Assert.h"

#include <algorithm>
#include <cstdint>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#include <sched.h>
#undef _GNU_SOURCE
#else
#include <sched.h>
#endif

namespace lib {

    /**
     * @brief CPU ID set to provide compact storage for a limited number of CPUs
     *
     * This wraps the cpu_set_t from libc, and has a fixed storage size, specified at construction (currently 512)
     *
     */
    class CpuIdSet {
        friend class Iterator;
        friend int sched_setaffinity(pid_t tid, const CpuIdSet & set);
        friend int sched_getaffinity(pid_t tid, CpuIdSet & set);

    public:
        class Iterator {
        public:
            Iterator(const CpuIdSet & set, uint16_t index) : set(set), index(index)
            {
                // If index doesn't point to a CPU in the set, advance it
                if (!set.contains(index)) {
                    ++(*this);
                }
            }

            Iterator & operator++()
            {
                if (index <= set.max_cpu) {
                    do {
                        index++;
                    } while (index <= set.max_cpu && !set.contains(index));
                }

                return *this;
            }

            uint16_t operator*() const
            {
                runtime_assert(set.contains(index), "Iterator points to invalid index");
                return index;
            }

            bool operator==(const Iterator & other) const { return index == other.index; }
            bool operator!=(const Iterator & other) const { return index != other.index; }

        private:
            const CpuIdSet & set;
            uint16_t index;
        };

        static constexpr const int MAX_CPUS = 512;

        CpuIdSet(size_t max_size = MAX_CPUS)
        {
            cpu_set = CPU_ALLOC(max_size);
            cpu_set_alloc_size = CPU_ALLOC_SIZE(max_size);
            cpu_set_size = max_size;
            min_cpu = max_cpu = 0;

            CPU_ZERO_S(cpu_set_alloc_size, cpu_set);
        }

        CpuIdSet(const CpuIdSet &) = delete;
        void operator=(const CpuIdSet &) = delete;

        CpuIdSet(CpuIdSet && other) noexcept
        {
            cpu_set = other.cpu_set;
            cpu_set_alloc_size = other.cpu_set_alloc_size;
            cpu_set_size = other.cpu_set_size;
            min_cpu = other.min_cpu;
            max_cpu = other.max_cpu;

            other.cpu_set = nullptr;
            other.cpu_set_alloc_size = 0;
            other.cpu_set_size = 0;
            other.min_cpu = 0;
            other.max_cpu = 0;
        }

        CpuIdSet & operator=(CpuIdSet && other) noexcept
        {
            if (cpu_set != nullptr) {
                CPU_FREE(cpu_set);
            }

            cpu_set = other.cpu_set;
            cpu_set_alloc_size = other.cpu_set_alloc_size;
            cpu_set_size = other.cpu_set_size;
            min_cpu = other.min_cpu;
            max_cpu = other.max_cpu;

            other.cpu_set = nullptr;
            other.cpu_set_alloc_size = 0;
            other.cpu_set_size = 0;
            other.min_cpu = 0;
            other.max_cpu = 0;

            return *this;
        }

        ~CpuIdSet()
        {
            if (cpu_set != nullptr) {
                CPU_FREE(cpu_set);
            }
        }

        void add(uint16_t cpu_id)
        {
            runtime_assert(cpu_id < cpu_set_size, "Tried to add CPU beyond set size");
            CPU_SET_S(cpu_id, cpu_set_alloc_size, cpu_set);

            min_cpu = std::min(min_cpu, cpu_id);
            max_cpu = std::max(max_cpu, cpu_id);
        }

        void remove(uint16_t cpu_id)
        {
            runtime_assert(cpu_id < cpu_set_size, "Tried to remove CPU beyond set size");
            CPU_CLR_S(cpu_id, cpu_set_alloc_size, cpu_set);
        }

        /// Clear values (but not storage)
        void clear()
        {
            CPU_ZERO_S(cpu_set_alloc_size, cpu_set);
            min_cpu = 0;
            max_cpu = 0;
        }

        [[nodiscard]] bool contains(uint16_t cpu_id) const { return CPU_ISSET_S(cpu_id, cpu_set_alloc_size, cpu_set); }

        [[nodiscard]] size_t count() const { return CPU_COUNT_S(cpu_set_alloc_size, cpu_set); }

        [[nodiscard]] bool empty() const { return count() == 0; }

        [[nodiscard]] Iterator begin() const { return {*this, min_cpu}; }
        [[nodiscard]] Iterator end() const { return {*this, static_cast<uint16_t>(max_cpu + 1)}; }

    private:
        cpu_set_t * cpu_set;

        /// Allocated size of the set (in bytes)
        size_t cpu_set_alloc_size;

        /// Size of the set in CPUs
        size_t cpu_set_size;

        /// Highest/lowest set members. @note These are not updated on CPU removal.
        uint16_t min_cpu, max_cpu;
    };

    inline int sched_setaffinity(pid_t tid, const CpuIdSet & set)
    {
        return sched_setaffinity(tid, set.cpu_set_alloc_size, set.cpu_set);
    }

    inline int sched_getaffinity(pid_t tid, CpuIdSet & set)
    {
        return sched_getaffinity(tid, set.cpu_set_alloc_size, set.cpu_set);
    }

} // End namespace lib
