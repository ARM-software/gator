/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#include "linux/proc/ProcessChildren.h"

#include <dirent.h>
#include <fstream>
#include <cstring>

namespace lnx
{
    static void addTidsRecursively(std::set<int> & tids, int tid)
    {
        auto result = tids.insert(tid);
        if (!result.second)
            return; // we've already added this and its children

        char filename[50];

        // try to get all children (forked processes), available since Linux 3.5
        snprintf(filename, sizeof(filename), "/proc/%d/task/%d/children", tid, tid);
        std::ifstream children { filename, std::ios_base::in };
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
        snprintf(filename, sizeof(filename), "/proc/%d/task", tid);
        DIR * const taskDir = opendir(filename);
        if (taskDir != nullptr) {
            const dirent * taskEntry;
            while ((taskEntry = readdir(taskDir)) != nullptr) {
                // no point recursing if we're relying on the fall back
                if (std::strcmp(taskEntry->d_name, ".") != 0 && std::strcmp(taskEntry->d_name, "..") != 0) {
                    const int child = atoi(taskEntry->d_name);
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

