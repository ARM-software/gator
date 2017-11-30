/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PROC_H
#define PROC_H

#include <stdint.h>

class Buffer;
class DynBuf;

bool readProcSysDependencies(uint64_t currTime, Buffer & buffer, DynBuf * const printb, DynBuf * const b1);
bool readProcMaps(uint64_t currTime, Buffer & buffer);
bool readKallsyms(const uint64_t currTime, Buffer * const buffer, const bool * const isDone);
bool readTracepointFormat(const uint64_t currTime, Buffer * const buffer, const char * const name,
                          DynBuf * const printb, DynBuf * const b);

#endif // PROC_H
