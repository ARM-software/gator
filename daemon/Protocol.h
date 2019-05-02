/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

// all values *must* be in range 0 ... 127 so as to fit in exactly one byte of packedInt
enum class FrameType : char
{
    UNKNOWN = 0,
    SUMMARY = 1,
    NAME = 3,
    COUNTER = 4,
    BLOCK_COUNTER = 5,
    SCHED_TRACE = 7,
    EXTERNAL = 10,
    PERF_ATTRS = 11,
    PROC = 11,
    PERF_DATA = 12,
    ACTIVITY_TRACE = 13,
    PERF_AUX = 14,
    PERF_SYNC = 15,
};

// PERF_ATTR messages
enum class CodeType : int
{
    PEA = 1,
    KEYS = 2,
    FORMAT = 3,
    MAPS = 4,
    COMM = 5,
    KEYS_OLD = 6,
    ONLINE_CPU = 7,
    OFFLINE_CPU = 8,
    KALLSYMS = 9,
    COUNTERS = 10,
    HEADER_PAGE = 11,
    HEADER_EVENT = 12,
};

// Summary Frame Messages
enum class MessageType : char
{
    LINK = 1,
    SUMMARY = 1,
    SCHED_SWITCH = 1,
    COOKIE_NAME = 1,
    THREAD_NAME = 2,
    THREAD_EXIT = 2,
    CORE_NAME = 3,
    TASK_EXIT = 3,
};

// From gator_marshaling.c
static constexpr const char * NEWLINE_CANARY  =
    /* Unix */
    "1\n"
    /* Windows */
    "2\r\n"
    /* Mac OS */
    "3\r"
    /* RISC OS */
    "4\n\r"
    /* Add another character so the length isn't 0x0a bytes */
    "5";


#endif // PROTOCOL_H
