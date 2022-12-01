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
namespace kbase {

// clang-format off
bool operator==(const version_check &lhs, const version_check &rhs);
inline bool operator!=(const version_check &lhs, const version_check &rhs) { return !(lhs == rhs); }
bool operator==(const set_flags &lhs, const set_flags &rhs);
inline bool operator!=(const set_flags &lhs, const set_flags &rhs) { return !(lhs == rhs); }
bool operator==(const get_gpuprops &lhs, const get_gpuprops &rhs);
inline bool operator!=(const get_gpuprops &lhs, const get_gpuprops &rhs) { return !(lhs == rhs); }
bool operator==(const cs_get_glb_iface::in_type &lhs, const cs_get_glb_iface::in_type &rhs);
inline bool operator!=(const cs_get_glb_iface::in_type &lhs, const cs_get_glb_iface::in_type &rhs) { return !(lhs == rhs); }
bool operator==(const cs_get_glb_iface::out_type &lhs, const cs_get_glb_iface::out_type &rhs);
inline bool operator!=(const cs_get_glb_iface::out_type &lhs, const cs_get_glb_iface::out_type &rhs) { return !(lhs == rhs); }
bool operator==(const hwcnt_reader_setup &lhs, const hwcnt_reader_setup &rhs);
inline bool operator!=(const hwcnt_reader_setup &lhs, const hwcnt_reader_setup &rhs) { return !(lhs == rhs); }
bool operator==(const kinstr_prfcnt_enum_info &lhs, const kinstr_prfcnt_enum_info &rhs);
inline bool operator!=(const kinstr_prfcnt_enum_info &lhs, const kinstr_prfcnt_enum_info &rhs) { return !(lhs == rhs); }
bool operator==(const kinstr_prfcnt_setup::in_type &lhs, const kinstr_prfcnt_setup::in_type &rhs);
inline bool operator!=(const kinstr_prfcnt_setup::in_type &lhs, const kinstr_prfcnt_setup::in_type &rhs) { return !(lhs == rhs); }
bool operator==(const kinstr_prfcnt_setup::out_type &lhs, const kinstr_prfcnt_setup::out_type &rhs);
inline bool operator!=(const kinstr_prfcnt_setup::out_type &lhs, const kinstr_prfcnt_setup::out_type &rhs) { return !(lhs == rhs); }
// clang-format on
} // namespace kbase
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
