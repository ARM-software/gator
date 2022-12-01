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
namespace vinstr {

std::ostream &operator<<(std::ostream &os, reader_event value);
std::ostream &operator<<(std::ostream &os, reader_features value);
std::ostream &operator<<(std::ostream &os, const reader_metadata_cycles &value);
std::ostream &operator<<(std::ostream &os, const reader_metadata &value);
std::ostream &operator<<(std::ostream &os, const reader_metadata_with_cycles &value);
std::ostream &operator<<(std::ostream &os, const reader_api_version &value);
std::ostream &operator<<(std::ostream &os, command::command_type cmd);

} // namespace vinstr
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
