/* Copyright (C) 2017-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_TIMESTAMPSOURCE_H
#define INCLUDE_LIB_TIMESTAMPSOURCE_H

#include <ctime>

namespace lib {
    /**
     * Provides current timestamp
     */
    class TimestampSource {
    public:
        TimestampSource(clockid_t id);

        [[nodiscard]] unsigned long long getBaseTimestampNS() const;
        [[nodiscard]] unsigned long long getTimestampNS() const;
        [[nodiscard]] unsigned long long getAbsTimestampNS() const;

    private:
        unsigned long long base;
        clockid_t id;
    };
}

#endif /* INCLUDE_LIB_TIMESTAMPSOURCE_H */
