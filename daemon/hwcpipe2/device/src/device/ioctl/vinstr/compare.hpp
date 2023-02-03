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

/* Note, this file is generated, do NOT edit! */

#pragma once

#include "commands.hpp"

namespace hwcpipe {
namespace device {
namespace ioctl {
namespace vinstr {

// clang-format off
bool operator==(const reader_metadata_cycles &lhs, const reader_metadata_cycles &rhs);
inline bool operator!=(const reader_metadata_cycles &lhs, const reader_metadata_cycles &rhs) { return !(lhs == rhs); }
bool operator==(const reader_metadata &lhs, const reader_metadata &rhs);
inline bool operator!=(const reader_metadata &lhs, const reader_metadata &rhs) { return !(lhs == rhs); }
bool operator==(const reader_metadata_with_cycles &lhs, const reader_metadata_with_cycles &rhs);
inline bool operator!=(const reader_metadata_with_cycles &lhs, const reader_metadata_with_cycles &rhs) { return !(lhs == rhs); }
bool operator==(const reader_api_version &lhs, const reader_api_version &rhs);
inline bool operator!=(const reader_api_version &lhs, const reader_api_version &rhs) { return !(lhs == rhs); }
// clang-format on
} // namespace vinstr
} // namespace ioctl
} // namespace device
} // namespace hwcpipe