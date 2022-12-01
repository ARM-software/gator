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

/** @file block_extents_operators.hpp */

#pragma once

#include <debug/ostream_indent.hpp>
#include <debug/print_array.hpp>
#include <device/hwcnt/block_extents.hpp>

#include <algorithm>
#include <cassert>
#include <ostream>

namespace hwcpipe {
namespace device {
namespace hwcnt {

inline bool operator==(const block_extents &lhs, const block_extents &rhs) {
    return lhs.num_blocks_of_type(block_type::fe) == rhs.num_blocks_of_type(block_type::fe)            //
           && lhs.num_blocks_of_type(block_type::tiler) == rhs.num_blocks_of_type(block_type::tiler)   //
           && lhs.num_blocks_of_type(block_type::memory) == rhs.num_blocks_of_type(block_type::memory) //
           && lhs.num_blocks_of_type(block_type::core) == rhs.num_blocks_of_type(block_type::core)     //
           && lhs.counters_per_block() == rhs.counters_per_block()                                     //
           && lhs.values_type() == rhs.values_type();
}

inline bool operator!=(const block_extents &lhs, const block_extents &rhs) { return !(lhs == rhs); }

inline std::ostream &operator<<(std::ostream &os, sample_values_type value) {
    switch (value) {
    case sample_values_type::uint32:
        return os << "uint32";
    case sample_values_type::uint64:
        return os << "uint64";
    }

    assert(!"Unknown sample_values_type value!");
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const block_extents &value) {
    const size_t num_blocks_of_type[] = {
        value.num_blocks_of_type(block_type::fe),     //
        value.num_blocks_of_type(block_type::tiler),  //
        value.num_blocks_of_type(block_type::memory), //
        value.num_blocks_of_type(block_type::core),   //
    };

    return os << "block_extents {\n"                                                                          //
              << debug::indent_level::push                                                                    //
              << debug::indent << ".num_blocks_of_type = " << debug::print_array(num_blocks_of_type) << ",\n" //
              << debug::indent << ".counters_per_block = " << value.counters_per_block() << ",\n"             //
              << debug::indent << ".values_type = " << value.values_type() << ",\n"                           //
              << debug::indent_level::pop                                                                     //
              << debug::indent << "}";                                                                        //
}

} // namespace hwcnt
} // namespace device
} // namespace hwcpipe
