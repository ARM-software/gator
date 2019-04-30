/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#include "linux/perf/PerfCpuOnlineMonitor.h"
#include "lib/FsEntry.h"

#include <cstring>
#include <sys/prctl.h>
#include <unistd.h>

PerfCpuOnlineMonitor::PerfCpuOnlineMonitor(NotificationCallback callback)
    : thread(),
      onlineCores(),
      callback(callback),
      terminated(false)
{
    thread = std::thread(launch, this);
}

PerfCpuOnlineMonitor::~PerfCpuOnlineMonitor()
{
    if (!terminated.load(std::memory_order_relaxed)) {
        terminate();
    }
}

void PerfCpuOnlineMonitor::terminate()
{
    terminated.store(true, std::memory_order_release);
    thread.join();
}

void PerfCpuOnlineMonitor::launch(PerfCpuOnlineMonitor * _this) noexcept
{
    _this->run();
}

void PerfCpuOnlineMonitor::run() noexcept
{
    // rename thread
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gatord-cpumon"), 0, 0, 0);

    // monitor filesystem
    bool firstPass = true;
    const lib::FsEntry sysFsCpuRootPath = lib::FsEntry::create("/sys/devices/system/cpu");
    while (!terminated.load(std::memory_order_acquire)) {
        // loop through files
        lib::Optional<lib::FsEntry> child;
        lib::FsEntryDirectoryIterator iterator = sysFsCpuRootPath.children();

        bool anyOffline = false;
        while ((child = iterator.next()).valid()) {
            const auto & name = child->name();
            if ((name.length() > 3) && (name.find("cpu") == 0)) {
                // find a CPU node
                const unsigned cpu = strtoul(name.c_str() + 3, nullptr, 10);
                // read its online state
                const lib::FsEntry onlineFsEntry = lib::FsEntry::create(*child, "online");
                const std::string contents = onlineFsEntry.readFileContentsSingleLine();
                if (!contents.empty()) {
                    const unsigned online = strtoul(contents.c_str(), nullptr, 0);
                    const bool isOnline = (online != 0);
                    anyOffline |= !isOnline;

                    // process it
                    process(firstPass, cpu, isOnline);
                }
            }
        }

        // sleep a little before checking again.
        // sleep longer if they are all online, otherwise just sleep a short amount of time so as to not miss the core coming back online by too much
        usleep(anyOffline ? 200 : 1000);

        // not first pass any more
        firstPass = false;
    }
}

void PerfCpuOnlineMonitor::process(bool first, unsigned cpu, bool online)
{
    if (online) {
        const auto insertionResult = onlineCores.insert(cpu);
        if (insertionResult.second && !first) {
            // set was modified so state changed from offline->online
            callback(cpu, true);
        }
    }
    else {
        const auto removalResult = onlineCores.erase(cpu);
        if ((removalResult > 0) && !first) {
            // set was modified so state changed from online->offline
            callback(cpu, false);
        }
    }
}
