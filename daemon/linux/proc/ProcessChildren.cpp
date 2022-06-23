/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcessChildren.h"

#include "lib/String.h"

#include <cstring>
#include <fstream>
#include <memory>

#include <dirent.h>

namespace lnx {
    static void addTidsRecursively(std::set<int> & tids, int tid)
    {
        auto result = tids.insert(tid);
        if (!result.second) {
            return; // we've already added this and its children
        }

        // try to get all children (forked processes), available since Linux 3.5
        lib::printf_str_t<64> filename {"/proc/%d/task/%d/children", tid, tid};
        std::ifstream children {filename, std::ios_base::in};
        if (children) {
            int child;
            while (children >> child) {
                addTidsRecursively(tids, child);
            }
        }

        // Now add all threads for the process
        // If 'children' is not found then new processes won't be counted on onlined cpu.
        // We could read /proc/[pid]/stat for every process and create a map in reverse
        // but that would likely be time consuming
        filename.printf("/proc/%d/task", tid);
        const std::unique_ptr<DIR, int (*)(DIR *)> taskDir {opendir(filename), &closedir};
        if (taskDir != nullptr) {
            const dirent * taskEntry;
            while ((taskEntry = readdir(taskDir.get())) != nullptr) {
                // no point recursing if we're relying on the fall back
                if (std::strcmp(taskEntry->d_name, ".") != 0 && std::strcmp(taskEntry->d_name, "..") != 0) {
                    const auto child = std::strtol(taskEntry->d_name, nullptr, 10);
                    if (child > 0) {
                        tids.insert(child);
                    }
                }
            }
        }
    }

    std::set<int> getChildTids(int tid)
    {
        std::set<int> result;
        addTidsRecursively(result, tid);
        return result;
    }
}
