/**
 * Copyright (C) Arm Limited 2013-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Tracepoints.h"

#include "linux/perf/IPerfAttrsConsumer.h"
#include "Config.h"
#include "Logging.h"
#include "lib/Utils.h"
#include "lib/Format.h"
#include <lib/FsEntry.h>

std::string getTracepointPath(const char * name, const char * file)
{
    return lib::Format() << EVENTS_PATH "/" << name << "/" << file;
}

bool readTracepointFormat(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, const char * const name)
{
    const lib::FsEntry file = lib::FsEntry::create(getTracepointPath(name, "format"));

    if (!file.canAccess(true, false, false)) {
        const std::string path = file.path();
        logg.logMessage("can't read %s", path.c_str());
        return false;
    }

    const std::string format = file.readFileContents();
    attrsConsumer.marshalFormat(currTime, format.size(), format.data());

    return true;
}

int64_t getTracepointId(const char * const name)
{
    int64_t result;
    if (lib::readInt64FromFile(getTracepointPath(name, "id").c_str(), result) != 0) {
        logg.logMessage("Unable to read tracepoint id for %s", name);
        return UNKNOWN_TRACEPOINT_ID;
    }

    return result;
}
