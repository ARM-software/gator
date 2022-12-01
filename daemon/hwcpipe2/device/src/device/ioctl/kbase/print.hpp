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
namespace kbase {

std::ostream &operator<<(std::ostream &os, const version_check &value);
std::ostream &operator<<(std::ostream &os, const set_flags &value);
std::ostream &operator<<(std::ostream &os, get_gpuprops::gpuprop_size value);
std::ostream &operator<<(std::ostream &os, get_gpuprops::gpuprop_code value);
std::ostream &operator<<(std::ostream &os, const get_gpuprops &value);
std::ostream &operator<<(std::ostream &os, const cs_get_glb_iface::in_type &value);
std::ostream &operator<<(std::ostream &os, const cs_get_glb_iface::out_type &value);
// no `operator<<` for union cs_get_glb_iface
std::ostream &operator<<(std::ostream &os, const hwcnt_reader_setup &value);
std::ostream &operator<<(std::ostream &os, const kinstr_prfcnt_enum_info &value);
std::ostream &operator<<(std::ostream &os, const kinstr_prfcnt_setup::in_type &value);
std::ostream &operator<<(std::ostream &os, const kinstr_prfcnt_setup::out_type &value);
// no `operator<<` for union kinstr_prfcnt_setup
std::ostream &operator<<(std::ostream &os, command::command_type cmd);

} // namespace kbase
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
