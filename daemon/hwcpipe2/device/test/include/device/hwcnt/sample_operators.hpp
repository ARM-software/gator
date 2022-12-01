/*
 * Copyright (c) 2022 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** @file sample_operators.hpp */

#pragma once

#include <debug/ostream_indent.hpp>
#include <device/hwcnt/sample.hpp>

#include <cassert>
#include <ostream>

namespace hwcpipe {
namespace device {
namespace hwcnt {

inline bool operator==(const sample_flags &lhs, const sample_flags &rhs) {
    return (lhs.stretched == rhs.stretched) && (lhs.error == rhs.error);
}

inline bool operator!=(const sample_flags &lhs, const sample_flags &rhs) { return !(lhs == rhs); }

inline std::ostream &operator<<(std::ostream &os, const sample_flags &value) {
    return os << "sample_flags {\n"
              << debug::indent_level::push                                    //
              << debug::indent << ".stretched = " << value.stretched << ",\n" //
              << debug::indent << ".error = " << value.error << ",\n"         //
              << debug::indent_level::pop                                     //
              << debug::indent << "}";                                        //
}

inline bool operator==(const sample_metadata &lhs, const sample_metadata &rhs) {
    return (lhs.user_data == rhs.user_data)                      //
           && (lhs.flags == rhs.flags)                           //
           && (lhs.sample_nr == rhs.sample_nr)                   //
           && (lhs.timestamp_ns_begin == rhs.timestamp_ns_begin) //
           && (lhs.timestamp_ns_end == rhs.timestamp_ns_end)     //
           && (lhs.gpu_cycle == rhs.gpu_cycle)                   //
           && (lhs.sc_cycle == rhs.sc_cycle);                    //
}

inline bool operator!=(const sample_metadata &lhs, const sample_metadata &rhs) { return !(lhs == rhs); }

inline std::ostream &operator<<(std::ostream &os, const sample_metadata &value) {
    return os << "sample_metadata {\n"
              << debug::indent_level::push                                                      //
              << debug::indent << ".user_data = " << value.user_data << ",\n"                   //
              << debug::indent << ".flags = " << value.flags << ",\n"                           //
              << debug::indent << ".sample_nr = " << value.sample_nr << ",\n"                   //
              << debug::indent << ".timestamp_ns_begin = " << value.timestamp_ns_begin << ",\n" //
              << debug::indent << ".timestamp_ns_end = " << value.timestamp_ns_end << ",\n"     //
              << debug::indent << ".gpu_cycle = " << value.gpu_cycle << ",\n"                   //
              << debug::indent << ".sc_cycle = " << value.sc_cycle << ",\n"                     //
              << debug::indent_level::pop                                                       //
              << debug::indent << "}";                                                          //
}

} // namespace hwcnt
} // namespace device
} // namespace hwcpipe
