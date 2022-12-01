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
namespace kbase_pre_r21 {

// clang-format off
bool operator==(const version_check_args &lhs, const version_check_args &rhs);
inline bool operator!=(const version_check_args &lhs, const version_check_args &rhs) { return !(lhs == rhs); }
bool operator==(const set_flags_args &lhs, const set_flags_args &rhs);
inline bool operator!=(const set_flags_args &lhs, const set_flags_args &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::core &lhs, const uk_gpuprops::gpu_props::core &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::core &lhs, const uk_gpuprops::gpu_props::core &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::l2_cache &lhs, const uk_gpuprops::gpu_props::l2_cache &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::l2_cache &lhs, const uk_gpuprops::gpu_props::l2_cache &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::tiler &lhs, const uk_gpuprops::gpu_props::tiler &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::tiler &lhs, const uk_gpuprops::gpu_props::tiler &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::thread &lhs, const uk_gpuprops::gpu_props::thread &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::thread &lhs, const uk_gpuprops::gpu_props::thread &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::raw &lhs, const uk_gpuprops::gpu_props::raw &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::raw &lhs, const uk_gpuprops::gpu_props::raw &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::coherent_group_info::coherent_group &lhs, const uk_gpuprops::gpu_props::coherent_group_info::coherent_group &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::coherent_group_info::coherent_group &lhs, const uk_gpuprops::gpu_props::coherent_group_info::coherent_group &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props::coherent_group_info &lhs, const uk_gpuprops::gpu_props::coherent_group_info &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props::coherent_group_info &lhs, const uk_gpuprops::gpu_props::coherent_group_info &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops::gpu_props &lhs, const uk_gpuprops::gpu_props &rhs);
inline bool operator!=(const uk_gpuprops::gpu_props &lhs, const uk_gpuprops::gpu_props &rhs) { return !(lhs == rhs); }
bool operator==(const uk_gpuprops &lhs, const uk_gpuprops &rhs);
inline bool operator!=(const uk_gpuprops &lhs, const uk_gpuprops &rhs) { return !(lhs == rhs); }
bool operator==(const uk_hwcnt_reader_setup &lhs, const uk_hwcnt_reader_setup &rhs);
inline bool operator!=(const uk_hwcnt_reader_setup &lhs, const uk_hwcnt_reader_setup &rhs) { return !(lhs == rhs); }
// clang-format on
} // namespace kbase_pre_r21
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
