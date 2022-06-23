/* Copyright (C) 2019-2022 by Arm Limited. All rights reserved. */

#include "linux/CoreOnliner.h"

#include "Logging.h"
#include "lib/String.h"
#include "lib/Utils.h"

#include <cstdio>

CoreOnliner::CoreOnliner(unsigned core) : core(core), known(false), changed(false), online(false)
{
    lib::printf_str_t<128> buffer {"/sys/devices/system/cpu/cpu%u/online", core};

    int64_t previous_online_status = 0;
    known = (lib::readInt64FromFile(buffer, previous_online_status) == 0);
    online = (previous_online_status != 0);
    changed = (known && !online ? (lib::writeCStringToFile(buffer, "1") == 0) //
                                : false);

    LOG_DEBUG("CoreOnliner(core=%u, known=%u, online=%u, changed=%u)", core, known, online, changed);
}

CoreOnliner::~CoreOnliner()
{
    if (changed) {
        lib::printf_str_t<128> buffer {"/sys/devices/system/cpu/cpu%u/online", core};
        if (lib::writeCStringToFile(buffer, "0") != 0) {
            LOG_ERROR("Failed to restore online state for %u", core);
        }
    }
}

CoreOnliner::CoreOnliner(CoreOnliner && that) noexcept : core(that.core), known(false), changed(false), online(false)
{
    std::swap(known, that.known);
    std::swap(online, that.online);
    std::swap(changed, that.changed);
}

CoreOnliner & CoreOnliner::operator=(CoreOnliner && that) noexcept
{
    CoreOnliner tmp(std::move(that));

    std::swap(core, tmp.core);
    std::swap(known, tmp.known);
    std::swap(online, tmp.online);
    std::swap(changed, tmp.changed);

    return *this;
}
