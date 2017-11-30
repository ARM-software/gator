/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FORMAT_H
#define INCLUDE_LIB_FORMAT_H

#include <sstream>

namespace lib
{
    /**
     * Helper class for formatting values into strings
     */
    class Format
    {
    public:

        /** @brief  Constructor */
        Format()
                : ss()
        {
        }

        /** @brief  Insertion operator */
        template<typename T>
        Format& operator <<(const T & that)
        {
            ss << that;
            return *this;
        }

        /** @brief  Convert to string */
        operator std::string() const
        {
            return ss.str();
        }

    private:

        /** @brief  Stream we use as buffer */
        std::ostringstream ss;
    };
}

#endif /* INCLUDE_LIB_FORMAT_H */
