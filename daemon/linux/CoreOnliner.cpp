/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#include "linux/CoreOnliner.h"

#include "Logging.h"
#include "lib/Utils.h"

#include <cstdio>

CoreOnliner::CoreOnliner(unsigned core)
        : core(core),
          known(false),
          changed(false),
          online(false)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/online", core);

    int64_t previous_online_status = 0;
    known = (lib::readInt64FromFile(buffer, previous_online_status) == 0);
    online = (previous_online_status != 0);
    changed = (known && !online ? (lib::writeCStringToFile(buffer, "1") == 0) //
                                : false);

    logg.logMessage("CoreOnliner(core=%u, known=%u, online=%u, changed=%u)", core, known, online, changed);
}

CoreOnliner::~CoreOnliner()
{
    if (changed) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "/sys/devices/system/cpu/cpu%u/online", core);
        if (lib::writeCStringToFile(buffer, "0") != 0) {
            logg.logError("Failed to restore online state for %u", core);
        }
    }
}

CoreOnliner::CoreOnliner(CoreOnliner && that)
    : core(that.core),
      known(false),
      changed(false),
      online(false)
{
    std::swap(known, that.known);
    std::swap(online, that.online);
    std::swap(changed, that.changed);
}

CoreOnliner& CoreOnliner::operator=(CoreOnliner && that)
{
    CoreOnliner tmp (std::move(that));

    std::swap(core, tmp.core);
    std::swap(known, tmp.known);
    std::swap(online, tmp.online);
    std::swap(changed, tmp.changed);

    return *this;
}
