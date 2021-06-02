/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_ISTREAM_H
#define INCLUDE_LIB_ISTREAM_H

#include <istream>
#include <vector>

namespace lib {
    /**
     * Extracts comma separated numbers from a stream.
     * @return vector of the numbers in the order they were in the stream
     */
    template<typename IntType>
    static std::vector<IntType> parseCommaSeparatedNumbers(std::istream & stream)
    {
        std::vector<IntType> ints {};

        IntType value;
        while (!(stream >> std::ws).eof() && stream >> value) {
            ints.push_back(value);
            if (!stream.eof()) {
                stream >> std::ws;
            }
            else {
                break;
            }
            if (!stream.eof() && stream.peek() != ',') {
                break;
            }
            if (!stream.eof()) {
                stream.get();
            }
        }
        return ints;
    }

}

#endif // INCLUDE_LIB_ISTREAM_H
