/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TRACEPOINTS_H
#define TRACEPOINTS_H

#include <stdint.h>
#include <string>

class IPerfAttrsConsumer;
class DynBuf;

/**
 *
 * @param name tracepoint name
 * @param file name of file within tracepoint directory
 * @return the path of the file for this tracepoint
 */
std::string getTracepointPath(const char * name, const char * file);

bool readTracepointFormat(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, const char * const name);

constexpr int64_t UNKNOWN_TRACEPOINT_ID = -1;
int64_t getTracepointId(const char * const name);

#endif // TRACEPOINTS_H
