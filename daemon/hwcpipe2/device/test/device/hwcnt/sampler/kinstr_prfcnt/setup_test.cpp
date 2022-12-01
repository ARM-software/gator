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

#include "union_init.hpp"

#include <catch2/catch.hpp>

#include <device/hwcnt/sampler/kinstr_prfcnt/setup.hpp>
#include <device/ioctl/kinstr_prfcnt/compare.hpp>
#include <device/mock/instance.hpp>
#include <device/mock/syscall/iface.hpp>

namespace hwcnt = hwcpipe::device::hwcnt;

namespace test {
namespace kinstr_prfcnt {
enum class failure_point {
    filter,
    kinstr_prfcnt_setup,
    mmap,
    none,
};

} // namespace kinstr_prfcnt

static constexpr int kinstr_prfcnt_fd = 43;
static constexpr uint32_t prfcnt_metadata_item_size = 1111;
static constexpr uint32_t prfcnt_mmap_size_bytes = 2222;
static constexpr uint64_t sc_mask = 0b1010;

static int mmap_data = 3333;
static void *mmap_addr = &mmap_data;

static constexpr uint8_t num_values = 123;

static constexpr std::array<hwcnt::sampler::configuration, 4> config{{
    {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, 0x1},
    {hwcnt::block_type::tiler, hwcnt::prfcnt_set::primary, 0x2},
    {hwcnt::block_type::memory, hwcnt::prfcnt_set::primary, 0x4},
    {hwcnt::block_type::core, hwcnt::prfcnt_set::primary, 0x8},
}};

} // namespace test

namespace mock {

class setup_helper {
  public:
    using enum_info_type = hwcnt::sampler::kinstr_prfcnt::enum_info;

    setup_helper(test::kinstr_prfcnt::failure_point failure)
        : failure_(failure) {}

    auto filter_block_extents(const instance & /* inst */, const hwcnt::sampler::configuration *begin,
                              const hwcnt::sampler::configuration *end) {
        CHECK(begin == test::config.begin());
        CHECK(end == test::config.end());

        if (failure_ == test::kinstr_prfcnt::failure_point::filter)
            return std::make_pair(std::make_error_code(std::errc::invalid_argument), hwcnt::block_extents{});

        hwcnt::block_extents extents{{{}}, test::num_values, hwcnt::sample_values_type::uint64};

        return std::make_pair(std::error_code{}, extents);
    }

    auto parse_enum_info(int fd, mock::syscall::iface & /* iface */) {
        CHECK(fd == mock::reference_data::mali_fd);

        enum_info_type ei{};
        ei.num_values = test::num_values;
        ei.has_cycles_top = true;
        return std::make_pair(std::error_code{}, ei);
    }

  private:
    test::kinstr_prfcnt::failure_point failure_;
};
} // namespace mock

TEST_CASE("device::hwcnt::sampler::kinstr_prfcnt::setup", "[unit]") {
    namespace ioctl = hwcpipe::device::ioctl;

    const test::kinstr_prfcnt::failure_point failure = GENERATE( //
        test::kinstr_prfcnt::failure_point::filter,              //
        test::kinstr_prfcnt::failure_point::kinstr_prfcnt_setup, //
        test::kinstr_prfcnt::failure_point::mmap,                //
        test::kinstr_prfcnt::failure_point::none                 //
    );
    mock::syscall::iface iface;

    uint32_t period_ns = GENERATE(0, 1000);
    namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;
    std::array<kinstr_prfcnt::request_item, 6> expected_request{{
        test::request_item::mode(period_ns),
        test::request_item::enable(kinstr_prfcnt::block_type::fe, kinstr_prfcnt::prfcnt_set::primary, 0x1, 0x0),
        test::request_item::enable(kinstr_prfcnt::block_type::tiler, kinstr_prfcnt::prfcnt_set::primary, 0x2, 0x0),
        test::request_item::enable(kinstr_prfcnt::block_type::memory, kinstr_prfcnt::prfcnt_set::primary, 0x4, 0x0),
        test::request_item::enable(kinstr_prfcnt::block_type::shader_core, kinstr_prfcnt::prfcnt_set::primary, 0x8,
                                   0x0),
        kinstr_prfcnt::request_item{},
    }};

    bool kinstr_prfcnt_fd_created = false;
    iface.ioctl_fn = [&](int fd, unsigned long command, void *argp) {
        CHECK(fd == mock::reference_data::mali_fd);

        REQUIRE(command == ioctl::kbase::command::kinstr_prfcnt_setup);

        using hwcpipe::device::ioctl::kbase::kinstr_prfcnt_setup;
        auto setup_arg = static_cast<kinstr_prfcnt_setup *>(argp);

        REQUIRE(argp);

        CHECK(setup_arg->in.request_item_count == expected_request.size());
        CHECK(setup_arg->in.request_item_size == sizeof(expected_request[0]));

        size_t i = 0;
        for (const auto &request_item : expected_request) {
            CHECK(setup_arg->in.requests_ptr.get()[i] == request_item);
            ++i;
        }

        if (failure == test::kinstr_prfcnt::failure_point::kinstr_prfcnt_setup)
            return std::make_pair(std::make_error_code(std::errc::invalid_argument), -1);

        setup_arg->out.prfcnt_metadata_item_size = test::prfcnt_metadata_item_size;
        setup_arg->out.prfcnt_mmap_size_bytes = test::prfcnt_mmap_size_bytes;

        kinstr_prfcnt_fd_created = true;

        return std::make_pair(std::error_code{}, test::kinstr_prfcnt_fd);
    };

    iface.mmap_fn = [&](void * /* addr */, size_t len, int /* prot */, int /* flags */, int fd, off_t /* off */) {
        CHECK(fd == test::kinstr_prfcnt_fd);
        CHECK(len == test::prfcnt_mmap_size_bytes);

        if (failure == test::kinstr_prfcnt::failure_point::mmap) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            return std::make_pair(std::make_error_code(std::errc::invalid_argument), MAP_FAILED);
        }

        return std::make_pair(std::error_code{}, test::mmap_addr);
    };

    iface.munmap_fn = [&](void *addr, size_t len) {
        CHECK(addr == test::mmap_addr);
        CHECK(len == test::prfcnt_mmap_size_bytes);

        return std::error_code{};
    };

    bool close_called = false;
    iface.close_fn = [&](int fd) {
        CHECK(fd == test::kinstr_prfcnt_fd);
        close_called = true;
        return std::error_code{};
    };

    mock::constants constants{};
    constants.shader_core_mask = test::sc_mask;

    mock::instance instance{hwcnt::backend_type::kinstr_prfcnt, constants};
    mock::setup_helper helper(failure);

    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::backend_args;
    backend_args<mock::syscall::iface> args{};

    std::error_code ec;

    using hwcpipe::device::hwcnt::sampler::kinstr_prfcnt::setup;
    std::tie(ec, args) = setup(instance, period_ns, test::config.begin(), test::config.end(), iface, helper);

    if (failure != test::kinstr_prfcnt::failure_point::none) {
        CHECK(ec);

        if (kinstr_prfcnt_fd_created)
            CHECK(close_called);

    } else {
        REQUIRE(!ec);

        CHECK(args.base_args.fd.get() == test::kinstr_prfcnt_fd);
        CHECK(args.base_args.period_ns == period_ns);

        {
            const auto &features = args.base_args.features_v;
            CHECK(!features.has_block_state);
            CHECK(features.has_stretched_flag);
            CHECK(features.overflow_behavior_defined);
            CHECK(features.has_gpu_cycle);
        }
        CHECK(args.base_args.extents.counters_per_block() == test::num_values);
        CHECK(args.base_args.memory.data() == test::mmap_addr);
        CHECK(args.base_args.memory.size() == test::prfcnt_mmap_size_bytes);

        CHECK(args.metadata_item_size == test::prfcnt_metadata_item_size);
        CHECK(args.sc_mask == test::sc_mask);
    }
}
