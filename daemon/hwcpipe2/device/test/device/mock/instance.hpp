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

#pragma once

#include <device/constants.hpp>
#include <device/hwcnt/backend_type.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/enum_info.hpp>
#include <device/kbase_version.hpp>

#include <utility>

namespace mock {

namespace hwcnt = hwcpipe::device::hwcnt;
namespace device = hwcpipe::device;

/** Mock constants structure. */
struct constants {
    uint64_t gpu_id;
    uint64_t num_l2_slices;
    uint64_t shader_core_mask;
};

namespace reference_data {
/** Default MALI file descriptor. */
static constexpr int mali_fd = 42;
/** Default block extents value. */
static const hwcnt::block_extents block_extents{{{1, 1, 2, 4}}, 64, hwcnt::sample_values_type::uint32};
/** Default kbase version. */
static constexpr hwcpipe::device::kbase_version kbase_version{1, 10, device::ioctl_iface_type::csf};
/** Default back-end type. */
static constexpr hwcnt::backend_type backend_type{hwcnt::backend_type::vinstr};
/** Default constants (Odin product id, 2 L2 slices, 4 shader cores). */
static constexpr constants constants_v{0xA004U, 2, 0b1111};
static constexpr hwcnt::sampler::kinstr_prfcnt::enum_info ei{
    hwcpipe::device::hwcnt::prfcnt_set::primary, 64, {{{1}, {1}, {1}, {4}}}, true, true};

} // namespace reference_data

/** Mock instance class. */
class instance {
  public:
    template <typename... args_t>
    instance(args_t &&...args) {
        set(std::forward<args_t>(args)...);
    }

    int fd() const { return fd_; }
    hwcnt::block_extents get_hwcnt_block_extents() const { return block_extents_; }
    device::kbase_version kbase_version() const { return kbase_version_; }
    hwcnt::backend_type backend_type() const { return backend_type_; }
    constants get_constants() const { return constants_; }
    hwcnt::sampler::kinstr_prfcnt::enum_info get_enum_info() const { return ei_; }

  private:
    template <typename head_t, typename... tail_t>
    void set(head_t &&head, tail_t &&...tail) {
        set(std::forward<head_t>(head));
        set(std::forward<tail_t>(tail)...);
    }

    void set(int fd) { fd_ = fd; }
    void set(hwcnt::block_extents extents) { block_extents_ = extents; }
    void set(device::kbase_version version) { kbase_version_ = version; }
    void set(hwcnt::backend_type type) { backend_type_ = type; }
    void set(constants constants_v) { constants_ = constants_v; }
    void set() {}

    /** Mali file descriptor. */
    int fd_{reference_data::mali_fd};
    /** Block extents. */
    hwcnt::block_extents block_extents_{reference_data::block_extents};
    /** Kbase version. */
    device::kbase_version kbase_version_{reference_data::kbase_version};
    /** Back-end type. */
    hwcnt::backend_type backend_type_{reference_data::backend_type};
    /** Constants structure. */
    constants constants_{reference_data::constants_v};
    /** Enum info */
    hwcnt::sampler::kinstr_prfcnt::enum_info ei_{reference_data::ei};
};

} // namespace mock
