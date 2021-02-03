/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "Tracepoints.h"

#include "Config.h"
#include "Logging.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "linux/perf/IPerfAttrsConsumer.h"

#include <array>
#include <fstream>
#include <string>
#include <unistd.h>

std::string getTracepointPath(const char * tracefsEventsPath, const char * name, const char * file)
{
    return lib::Format() << tracefsEventsPath << "/" << name << "/" << file;
}

bool readTracepointFormat(IPerfAttrsConsumer & attrsConsumer, const char * tracefsEventsPath, const char * name)
{
    const lib::FsEntry file = lib::FsEntry::create(getTracepointPath(tracefsEventsPath, name, "format"));

    if (!file.canAccess(true, false, false)) {
        const std::string path = file.path();
        logg.logMessage("can't read %s", path.c_str());
        return false;
    }

    const std::string format = file.readFileContents();
    attrsConsumer.marshalFormat(format.size(), format.data());

    return true;
}

int64_t getTracepointId(const char * tracefsEventsPath, const char * const name)
{
    int64_t result;
    if (lib::readInt64FromFile(getTracepointPath(tracefsEventsPath, name, "id").c_str(), result) != 0) {
        logg.logMessage("Unable to read tracepoint id for %s", name);
        return UNKNOWN_TRACEPOINT_ID;
    }

    return result;
}

namespace {
    static const std::array<TraceFsConstants, 2> TRACEFS_CONFIGURATIONS {{// The usual configuration on most systems
                                                                          {"/sys/kernel/debug/tracing",
                                                                           "/sys/kernel/debug/tracing/events",
                                                                           "/sys/kernel/debug/tracing/events/enable",
                                                                           "/sys/kernel/debug/tracing/events/ftrace",
                                                                           "/sys/kernel/debug/tracing/trace",
                                                                           "/sys/kernel/debug/tracing/trace_clock",
                                                                           "/sys/kernel/debug/tracing/trace_pipe",
                                                                           "/sys/kernel/debug/tracing/tracing_on"},
                                                                          // Android R (no debugfs)
                                                                          {"/sys/kernel/tracing",
                                                                           "/sys/kernel/tracing/events",
                                                                           "/sys/kernel/tracing/events/enable",
                                                                           "/sys/kernel/tracing/events/ftrace",
                                                                           "/sys/kernel/tracing/trace",
                                                                           "/sys/kernel/tracing/trace_clock",
                                                                           "/sys/kernel/tracing/trace_pipe",
                                                                           "/sys/kernel/tracing/tracing_on"}}};

    class TraceFsConstantsWrapper {
    public:
        TraceFsConstantsWrapper(std::string path)
            : path(std::move(path)),
              path__events(this->path + "/events"),
              path__events__enable(this->path + "/events/enable"),
              path__events__ftrace(this->path + "/events/ftrace"),
              path__trace(this->path + "/trace"),
              path__trace_clock(this->path + "/trace_clock"),
              path__trace_pipe(this->path + "/trace_pipe"),
              path__tracing_on(this->path + "/tracing_on"),
              constants()
        {
            constants.path = this->path.c_str();
            constants.path__events = this->path__events.c_str();
            constants.path__events__enable = this->path__events__enable.c_str();
            constants.path__events__ftrace = this->path__events__ftrace.c_str();
            constants.path__trace = this->path__trace.c_str();
            constants.path__trace_clock = this->path__trace_clock.c_str();
            constants.path__trace_pipe = this->path__trace_pipe.c_str();
            constants.path__tracing_on = this->path__tracing_on.c_str();
        }

    private:
        // TRACING_PATH
        std::string path;
        // TRACING_PATH "/events"
        std::string path__events;
        // TRACING_PATH "/events/enable"
        std::string path__events__enable;
        // TRACING_PATH "/events/ftrace"
        std::string path__events__ftrace;
        // TRACING_PATH "/trace"
        std::string path__trace;
        // TRACING_PATH "/trace_clock"
        std::string path__trace_clock;
        // TRACING_PATH "/trace_pipe"
        std::string path__trace_pipe;
        // TRACING_PATH "/tracing_on"
        std::string path__tracing_on;

    public:
        TraceFsConstants constants;
    };

    /** Parse /proc/mounts, looking for tracefs mount point */
    static const TraceFsConstants * findTraceFsMount()
    {
        static std::unique_ptr<TraceFsConstantsWrapper> pointer;

        // check we've not been here before
        if (pointer != nullptr) {
            return &pointer->constants;
        }

        logg.logMessage("Reading /proc/mounts");

        // iterate each line of /proc/mounts
        std::ifstream file("/proc/mounts", std::ios_base::in);
        for (std::string line; std::getline(file, line);) {
            logg.logMessage("    '%s'", line.c_str());

            // find the mount point section of the string, provided it is a tracefs mount
            const auto indexOfFirstSep = line.find(" /");
            if (indexOfFirstSep == std::string::npos) {
                continue;
            }
            const auto indexOfTraceFs = line.find(" tracefs", indexOfFirstSep + 1);
            if (indexOfTraceFs == std::string::npos) {
                continue;
            }

            // found it
            auto mountPoint = line.substr(indexOfFirstSep + 1, indexOfTraceFs - (indexOfFirstSep + 1));
            logg.logMessage("Found tracefs at '%s'", mountPoint.c_str());

            if (lib::access(mountPoint.c_str(), R_OK) == 0) {
                // check it is not one of the baked in configurations, reuse it instead of constructing a new item
                for (const auto & config : TRACEFS_CONFIGURATIONS) {
                    if (mountPoint == config.path) {
                        return &config;
                    }
                }

                // OK, construct a new item
                pointer.reset(new TraceFsConstantsWrapper(std::move(mountPoint)));
                return &pointer->constants;
            }
        }

        return nullptr;
    }
}

const TraceFsConstants & TraceFsConstants::detect()
{
    // try to read from /proc/mounts first
    const auto mountPoint = findTraceFsMount();
    if (mountPoint != nullptr) {
        return *mountPoint;
    }

    // try some defaults
    for (const auto & config : TRACEFS_CONFIGURATIONS) {
        if (lib::access(config.path, R_OK) == 0) {
            return config;
        }
    }

    // just use the first one (usual for linux) as some placeholder default
    return TRACEFS_CONFIGURATIONS[0];
}
