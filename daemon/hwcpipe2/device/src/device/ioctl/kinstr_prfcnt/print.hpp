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
namespace kinstr_prfcnt {

std::ostream &operator<<(std::ostream &os, block_type value);
std::ostream &operator<<(std::ostream &os, prfcnt_set value);
std::ostream &operator<<(std::ostream &os, enum_item::item_type value);
std::ostream &operator<<(std::ostream &os, const enum_item::header &value);
std::ostream &operator<<(std::ostream &os, const enum_item::enum_block_counter &value);
std::ostream &operator<<(std::ostream &os, enum_item::enum_request::request_type value);
std::ostream &operator<<(std::ostream &os, const enum_item::enum_request &value);
std::ostream &operator<<(std::ostream &os, const enum_item::enum_sample_info &value);
// no `operator<<` for union enum_item::enum_union
std::ostream &operator<<(std::ostream &os, const enum_item &value);
std::ostream &operator<<(std::ostream &os, metadata_item::item_type value);
std::ostream &operator<<(std::ostream &os, const metadata_item::header &value);
std::ostream &operator<<(std::ostream &os, metadata_item::block_metadata::block_state_type value);
std::ostream &operator<<(std::ostream &os, const metadata_item::block_metadata &value);
std::ostream &operator<<(std::ostream &os, const metadata_item::clock_metadata &value);
std::ostream &operator<<(std::ostream &os, metadata_item::sample_metadata::sample_flag value);
std::ostream &operator<<(std::ostream &os, const metadata_item::sample_metadata &value);
// no `operator<<` for union metadata_item::metadata_union
std::ostream &operator<<(std::ostream &os, const metadata_item &value);
std::ostream &operator<<(std::ostream &os, control_cmd::control_cmd_code value);
std::ostream &operator<<(std::ostream &os, const control_cmd &value);
std::ostream &operator<<(std::ostream &os, request_item::item_type value);
std::ostream &operator<<(std::ostream &os, const request_item::header &value);
std::ostream &operator<<(std::ostream &os, request_item::request_mode::sampling_mode value);
std::ostream &operator<<(std::ostream &os, const request_item::request_mode::periodic_type &value);
// no `operator<<` for union request_item::request_mode::mode_config_union
std::ostream &operator<<(std::ostream &os, const request_item::request_mode &value);
std::ostream &operator<<(std::ostream &os, const request_item::request_enable &value);
std::ostream &operator<<(std::ostream &os, request_item::request_scope::counters_scope value);
std::ostream &operator<<(std::ostream &os, const request_item::request_scope &value);
// no `operator<<` for union request_item::request_union
std::ostream &operator<<(std::ostream &os, const request_item &value);
std::ostream &operator<<(std::ostream &os, const sample_access &value);
std::ostream &operator<<(std::ostream &os, command::command_type cmd);

} // namespace kinstr_prfcnt
} // namespace ioctl
} // namespace device
} // namespace hwcpipe
