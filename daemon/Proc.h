/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef PROC_H
#define PROC_H

#include <atomic>
#include <stdint.h>

class IPerfAttrsConsumer;
class DynBuf;
class FtraceDriver;

bool readProcSysDependencies(uint64_t currTime,
                             IPerfAttrsConsumer & attrsConsumer,
                             DynBuf * const printb,
                             DynBuf * const b1,
                             FtraceDriver & ftraceDriver);
bool readProcMaps(uint64_t currTime, IPerfAttrsConsumer & attrsConsumer);
bool readKallsyms(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, const std::atomic_bool & isDone);

#endif // PROC_H
