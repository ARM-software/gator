/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#ifndef PROC_H
#define PROC_H

class IPerfAttrsConsumer;
class DynBuf;
class FtraceDriver;

bool readProcSysDependencies(IPerfAttrsConsumer & buffer, DynBuf * printb, DynBuf * b1, FtraceDriver & ftraceDriver);

void readKernelBuildId(IPerfAttrsConsumer & attrsConsumer);
void readModuleBuildIds(IPerfAttrsConsumer & attrsConsumer);

#endif // PROC_H
