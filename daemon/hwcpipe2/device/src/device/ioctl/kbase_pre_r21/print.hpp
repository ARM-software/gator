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

#include <ostream>

namespace hwcpipe {
namespace device {
namespace ioctl {
namespace kbase_pre_r21 {

std::ostream &operator<<(std::ostream &os, header_id value);
// no `operator<<` for union uk_header
std::ostream &operator<<(std::ostream &os, const version_check_args &value);
std::ostream &operator<<(std::ostream &os, const set_flags_args &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::core &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::l2_cache &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::tiler &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::thread &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::raw &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::coherent_group_info::coherent_group &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props::coherent_group_info &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops::gpu_props &value);
std::ostream &operator<<(std::ostream &os, const uk_gpuprops &value);
std::ostream &operator<<(std::ostream &os, const uk_hwcnt_reader_setup &value);
std::ostream &operator<<(std::ostream &os, command::command_type cmd);

} // namespace kbase_pre_r21
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
