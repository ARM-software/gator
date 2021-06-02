/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef TRACEPOINTS_H
#define TRACEPOINTS_H

#include <cstdint>
#include <string>

class IPerfAttrsConsumer;
class DynBuf;

/**
 * Contains the set of paths we care about within tracefs.
 * Refer to https://www.kernel.org/doc/Documentation/trace/ftrace.txt
 * for more information about ftrace / tracefs
 */
struct TraceFsConstants {
    // TRACING_PATH
    const char * path;
    // TRACING_PATH "/events"
    const char * path__events;
    // TRACING_PATH "/events/enable"
    const char * path__events__enable;
    // TRACING_PATH "/events/ftrace"
    const char * path__events__ftrace;
    // TRACING_PATH "/trace"
    const char * path__trace;
    // TRACING_PATH "/trace_clock"
    const char * path__trace_clock;
    // TRACING_PATH "/trace_pipe"
    const char * path__trace_pipe;
    // TRACING_PATH "/tracing_on"
    const char * path__tracing_on;

    // return the appropriate path set for this machine
    static const TraceFsConstants & detect();
};

/**
 * @param name tracepoint name
 * @param file name of file within tracepoint directory
 * @return the path of the file for this tracepoint
 */
std::string getTracepointPath(const char * tracefsEventsPath, const char * name, const char * file);

inline std::string getTracepointPath(const TraceFsConstants & constants, const char * name, const char * file)
{
    return getTracepointPath(constants.path__events, name, file);
}

bool readTracepointFormat(IPerfAttrsConsumer & attrsConsumer, const char * tracefsEventsPath, const char * name);

inline bool readTracepointFormat(IPerfAttrsConsumer & attrsConsumer,
                                 const TraceFsConstants & constants,
                                 const char * name)
{
    return readTracepointFormat(attrsConsumer, constants.path__events, name);
}

constexpr int64_t UNKNOWN_TRACEPOINT_ID = -1;

int64_t getTracepointId(const char * tracefsEventsPath, const char * name);

inline int64_t getTracepointId(const TraceFsConstants & constants, const char * name)
{
    return getTracepointId(constants.path__events, name);
}

#endif // TRACEPOINTS_H
