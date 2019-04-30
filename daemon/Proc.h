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
#include <atomic>

class IPerfAttrsConsumer;
class DynBuf;
class FtraceDriver;

bool readProcSysDependencies(uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, DynBuf * const printb, DynBuf * const b1, FtraceDriver & ftraceDriver);
bool readProcMaps(uint64_t currTime, IPerfAttrsConsumer & attrsConsumer);
bool readKallsyms(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, const std::atomic_bool & isDone);


#endif // PROC_H
