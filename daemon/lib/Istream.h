/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_ISTREAM_H
#define INCLUDE_LIB_ISTREAM_H

#include <istream>

namespace lib
{
    /**
     * Extracts comma separated numbers from a stream.
     * Sets failbit on parse error
     * @param stream
     * @return vector of the numbers in the order they were in the stream
     */
    template<typename IntType>
    static std::vector<IntType> parseCommaSeparatedNumbers(std::istream& stream)
    {
        std::vector<IntType> ints { };

        IntType value;
        while (!(stream >> std::ws).eof() && stream >> value) {
            ints.push_back(value);
            stream >> std::ws;
            if (!stream.eof() && stream.get() != ',') {
                stream.setstate(std::ios_base::failbit);
            }
        }

        return ints;
    }

}

#endif // INCLUDE_LIB_ISTREAM_H

