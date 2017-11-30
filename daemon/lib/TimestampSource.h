/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_TIMESTAMPSOURCE_H
#define INCLUDE_LIB_TIMESTAMPSOURCE_H

#include <ctime>

namespace lib
{
    /**
     * Provides current timestamp
     */
    class TimestampSource
    {
    public:

        TimestampSource(clockid_t id);

        unsigned long long getBaseTimestampNS() const;
        unsigned long long getTimestampNS() const;
        unsigned long long getAbsTimestampNS() const;

    private:

        unsigned long long base;
        clockid_t id;
    };
}

#endif /* INCLUDE_LIB_TIMESTAMPSOURCE_H */
