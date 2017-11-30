/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "lib/TimestampSource.h"

namespace lib
{
    TimestampSource::TimestampSource(clockid_t id_)
            : base(0),
              id(id_)
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

        return ts.tv_sec * 1000000000ull + ts.tv_nsec;
    }
}
