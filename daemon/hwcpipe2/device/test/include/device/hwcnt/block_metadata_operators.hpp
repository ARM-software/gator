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

/** @file block_metadata_operators.hpp */

#pragma once

#include <debug/ostream_indent.hpp>
#include <device/hwcnt/block_metadata.hpp>

#include <cassert>
#include <ostream>

namespace hwcpipe {
namespace device {
namespace hwcnt {

inline std::ostream &operator<<(std::ostream &os, block_type value) {
    switch (value) {
    case block_type::fe:
        return os << "fe";
    case block_type::tiler:
        return os << "tiler";
    case block_type::memory:
        return os << "memory";
    case block_type::core:
        return os << "core";
    default:
        assert(!"Unexpected block_type.");
        return os;
    }
}

inline std::ostream &operator<<(std::ostream &os, prfcnt_set value) {
    switch (value) {
    case prfcnt_set::primary:
        return os << "primary";
    case prfcnt_set::secondary:
        return os << "secondary";
    case prfcnt_set::tertiary:
        return os << "tertiary";
    default:
        assert(!"Unexpected prfcnt_set.");
        return os;
    }
}

inline bool operator==(const block_state &lhs, const block_state &rhs) {
    return (lhs.on == rhs.on)                             //
           && (lhs.off == rhs.off)                        //
           && (lhs.available == rhs.available)            //
           && (lhs.unavailable == rhs.unavailable)        //
           && (lhs.normal == rhs.normal)                  //
           && (lhs.protected_mode == rhs.protected_mode); //
}

inline bool operator!=(const block_state &lhs, const block_state &rhs) { return !(lhs == rhs); }

inline std::ostream &operator<<(std::ostream &os, const block_state &value) {
    return os << "block_state {\n"
              << debug::indent_level::push                                              //
              << debug::indent << ".on = " << value.on << ",\n"                         //
              << debug::indent << ".off = " << value.off << ",\n"                       //
              << debug::indent << ".available = " << value.available << ",\n"           //
              << debug::indent << ".unavailable = " << value.unavailable << ",\n"       //
              << debug::indent << ".normal = " << value.normal << ",\n"                 //
              << debug::indent << ".protected_mode = " << value.protected_mode << ",\n" //
              << debug::indent_level::pop                                               //
              << debug::indent << "}";                                                  //
}

inline bool operator==(const block_metadata &lhs, const block_metadata &rhs) {
    return (lhs.type == rhs.type)         //
           && (lhs.index == rhs.index)    //
           && (lhs.set == rhs.set)        //
           && (lhs.state == rhs.state)    //
           && (lhs.values == rhs.values); //
}

inline bool operator!=(const block_metadata &lhs, const block_metadata &rhs) { return !(lhs == rhs); }

inline std::ostream &operator<<(std::ostream &os, const block_metadata &value) {
    return os << "block_metadata {\n"
              << debug::indent_level::push                              //
              << debug::indent << ".type = " << value.type << ",\n"     //
              << debug::indent << ".index = " << value.index << ",\n"   //
              << debug::indent << ".set = " << value.set << ",\n"       //
              << debug::indent << ".state = " << value.state << ",\n"   //
              << debug::indent << ".values = " << value.values << ",\n" //
              << debug::indent_level::pop                               //
              << debug::indent << "}";                                  //
}

} // namespace hwcnt
} // namespace device
} // namespace hwcpipe
