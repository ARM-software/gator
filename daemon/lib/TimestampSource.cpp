/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

#include "lib/TimestampSource.h"

#include <ctime>

namespace lib {
    TimestampSource::TimestampSource(clockid_t id_) : base(0), id(id_)
    {
        base = getAbsTimestampNS();
    }

    unsigned long long TimestampSource::getBaseTimestampNS() const
    {
        return base;
    }

    unsigned long long TimestampSource::getTimestampNS() const
    {
        return getAbsTimestampNS() - base;
    }

    unsigned long long TimestampSource::getAbsTimestampNS() const
    {
        ::timespec ts;
        clock_gettime(id, &ts);

        return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }
}
