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

#include <catch2/catch.hpp>

#include <device/hwcnt/block_extents_operators.hpp>
#include <device/hwcnt/sampler/vinstr/setup.hpp>
#include <device/ioctl/vinstr/types.hpp>
#include <device/mock/instance.hpp>
#include <device/mock/syscall/iface.hpp>

#include <array>
#include <cstring>
#include <set>
#include <tuple>

#include <sys/mman.h>

namespace hwcnt = hwcpipe::device::hwcnt;

namespace test {
namespace vinstr {
enum class failure_point {
    filter,
    prfcnt_set,
    ioctl_setup,
    ioctl_get_api_version,
    ioctl_get_buffer_size,
    mmap,
    none,
};

} // namespace vinstr

static constexpr int vinstr_fd = 43;
static constexpr uint32_t buffer_size = 1024;
static constexpr auto mmap_size = buffer_size * 32;
static int mmap_data = 3333;
static void *mmap_addr = &mmap_data;

static constexpr uint64_t num_l2_slices = 2;
static constexpr uint64_t shader_core_mask = 0b1111;

static const std::array<hwcnt::sampler::configuration, 4> config{{
    {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, 0x1},
    {hwcnt::block_type::tiler, hwcnt::prfcnt_set::primary, 0x2},
    {hwcnt::block_type::memory, hwcnt::prfcnt_set::primary, 0x4},
    {hwcnt::block_type::core, hwcnt::prfcnt_set::primary, 0x8},
}};

} // namespace test

TEST_CASE("device::hwcnt::sampler::vinstr::setup", "[unit]") {
    namespace ioctl = hwcpipe::device::ioctl;

    const uint64_t period_ns = GENERATE(0, 1000);
    const ioctl::vinstr::reader_api_version reader_api_version{
        0,
        GENERATE(                                       //
            ioctl::vinstr::reader_features{},           //
            ioctl::vinstr::reader_features::cycles_top, //
            ioctl::vinstr::reader_features::cycles_top | ioctl::vinstr::reader_features::cycles_shader_core),
    };

    using hwcpipe::device::hwcnt::sampler::vinstr::sample_layout_type;
    uint64_t gpu_id{};
    sample_layout_type expected_layout_type{};
    uint8_t num_mem_blocks{};

    std::tie(gpu_id, expected_layout_type, num_mem_blocks) = GENERATE_COPY(                               //
        std::make_tuple(0x0750ULL, sample_layout_type::v4, static_cast<uint8_t>(1)),                      //
        std::make_tuple(0x0760ULL, sample_layout_type::non_v4, static_cast<uint8_t>(test::num_l2_slices)) //
    );

    const auto failure = GENERATE(                          //
        test::vinstr::failure_point::filter,                //
        test::vinstr::failure_point::prfcnt_set,            //
        test::vinstr::failure_point::ioctl_setup,           //
        test::vinstr::failure_point::ioctl_get_api_version, //
        test::vinstr::failure_point::ioctl_get_buffer_size, //
        test::vinstr::failure_point::mmap,                  //
        test::vinstr::failure_point::none                   //
    );

    using hwcpipe::device::ioctl_iface_type;

    const auto version_type = GENERATE( //
        ioctl_iface_type::jm_pre_r21,   //
        ioctl_iface_type::jm_post_r21   //
    );

    const hwcpipe::device::kbase_version kbase_version{10, 0, version_type};
    const hwcnt::block_extents expected_extents{{{1, 1, num_mem_blocks, 4}}, 64, hwcnt::sample_values_type::uint32};

    mock::syscall::iface iface;

    bool vinstr_fd_created = false;
    iface.ioctl_fn = [&](int fd, unsigned long command, void *argp) {
        switch (command) {
        case ioctl::kbase_pre_r21::command::hwcnt_reader_setup: {
            CHECK(fd == mock::reference_data::mali_fd);
            CHECK(version_type == ioctl_iface_type::jm_pre_r21);

            if (failure == test::vinstr::failure_point::ioctl_setup)
                return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

            vinstr_fd_created = true;

            using ioctl::kbase_pre_r21::uk_hwcnt_reader_setup;
            using ioctl::kbase_pre_r21::header_id;

            REQUIRE(argp);

            auto &setup_args = *static_cast<uk_hwcnt_reader_setup *>(argp);

            CHECK(setup_args.header.id == header_id::hwcnt_reader_setup);
            setup_args.fd = test::vinstr_fd;

            return std::make_pair(std::error_code{}, 0);
        }

        case ioctl::kbase::command::hwcnt_reader_setup:
            CHECK(fd == mock::reference_data::mali_fd);
            CHECK(version_type == ioctl_iface_type::jm_post_r21);

            if (failure == test::vinstr::failure_point::ioctl_setup)
                return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

            vinstr_fd_created = true;

            return std::make_pair(std::error_code{}, test::vinstr_fd);

        case ioctl::vinstr::command::get_api_version:
            CHECK(fd == test::vinstr_fd);

            if (failure == test::vinstr::failure_point::ioctl_get_api_version)
                return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

            memcpy(argp, &reader_api_version.version, sizeof(reader_api_version.version));
            return std::make_pair(std::error_code{}, 0);

        case ioctl::vinstr::command::get_api_version_with_features:
            CHECK(fd == test::vinstr_fd);

            if (failure == test::vinstr::failure_point::ioctl_get_api_version)
                return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

            memcpy(argp, &reader_api_version, sizeof(reader_api_version));
            return std::make_pair(std::error_code{}, 0);

        case ioctl::vinstr::command::get_buffer_size:
            CHECK(fd == test::vinstr_fd);

            if (failure == test::vinstr::failure_point::ioctl_get_buffer_size)
                return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

            memcpy(argp, &test::buffer_size, sizeof(test::buffer_size));
            return std::make_pair(std::error_code{}, 0);
        }

        FAIL();
        return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);
    };

    iface.mmap_fn = [&](void * /* addr */, size_t len, int /* prot */, int /* flags */, int fd, off_t /* off */) {
        CHECK(fd == test::vinstr_fd);
        CHECK(len == test::mmap_size);

        if (failure == test::vinstr::failure_point::mmap) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            return std::make_pair(std::make_error_code(std::errc::invalid_argument), MAP_FAILED);
        }

        return std::make_pair(std::error_code{}, test::mmap_addr);
    };

    iface.munmap_fn = [&](void *addr, size_t len) {
        CHECK(addr == test::mmap_addr);
        CHECK(len == test::mmap_size);

        return std::error_code{};
    };

    bool close_called = false;
    iface.close_fn = [&](int fd) {
        CHECK(fd == test::vinstr_fd);
        close_called = true;
        return std::error_code{};
    };

    using hwcpipe::device::hwcnt::sampler::vinstr::backend_args;
    backend_args<mock::syscall::iface> args{};

    std::error_code ec;

    using hwcpipe::device::hwcnt::sampler::vinstr::setup;

    auto config = test::config;

    if (failure == test::vinstr::failure_point::prfcnt_set)
        config[0].set = hwcnt::prfcnt_set::secondary;

    if (failure == test::vinstr::failure_point::filter)
        config[0].type = hwcnt::block_type::tiler;

    mock::constants constants{};
    constants.gpu_id = gpu_id;
    constants.num_l2_slices = test::num_l2_slices;
    constants.shader_core_mask = test::shader_core_mask;

    std::tie(ec, args) = setup(mock::instance{kbase_version, expected_extents, constants}, period_ns, config.begin(),
                               config.end(), iface);

    if (failure != test::vinstr::failure_point::none) {
        CHECK(ec);

        if (vinstr_fd_created)
            CHECK(close_called);
    } else {
        REQUIRE(!ec);

        CHECK(args.base_args.fd.get() == test::vinstr_fd);
        CHECK(args.base_args.period_ns == period_ns);

        {
            const auto &features = args.base_args.features_v;
            CHECK(!features.has_block_state);
            CHECK(!features.has_stretched_flag);
            CHECK(features.overflow_behavior_defined);
            CHECK(features.has_gpu_cycle == !!reader_api_version.features);
        }
        CHECK(args.base_args.extents == expected_extents);
        CHECK(args.base_args.memory.data() == test::mmap_addr);
        CHECK(args.base_args.memory.size() == test::mmap_size);

        CHECK(args.features == reader_api_version.features);
        CHECK(args.buffer_size == test::buffer_size);

        CHECK(args.sample_layout_v.size() == expected_extents.num_blocks());
        CHECK(args.sample_layout_v.get_sample_layout_type() == expected_layout_type);
    }
}
