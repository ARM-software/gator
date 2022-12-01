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

#include <device/hwcnt/block_extents_operators.hpp>
#include <device/hwcnt/block_metadata_operators.hpp>
#include <device/hwcnt/sample_operators.hpp>
#include <device/hwcnt/sampler/kinstr_prfcnt/backend.hpp>
#include <device/ioctl/kinstr_prfcnt/compare.hpp>
#include <device/ioctl/kinstr_prfcnt/print.hpp>
#include <device/mock/syscall/iface.hpp>

#include <vector>

namespace hwcnt = hwcpipe::device::hwcnt;
namespace ioctl = hwcpipe::device::ioctl;

namespace test {
/** kinstr_prfcnt file descriptor. */
static constexpr int kinstr_prfcnt_fd = 42;

/** User data. */
static constexpr uint64_t user_data = 1111;

/** Sample number. */
static constexpr uint64_t sample_nr = 2222;

/** Sample metadata. */
static hwcnt::sample_metadata sample_metadata{
    user_data, // user_data;
    {},        // flags;
    sample_nr, // sample_nr;
    3333,      // timestamp_ns_begin;
    4444,      // timestamp_ns_end;
    5555,      // gpu_cycle;
    6666,      // sc_cycle;
};

/** Block extents. */
static const hwcnt::block_extents extents = {{{1, 1, 2, 4}}, 64, hwcnt::sample_values_type::uint64};

/** Sample metadata offset in the dump buffer. */
static constexpr size_t metadata_offset = 512;

/** Metadata item size. */
static constexpr size_t metadata_item_size = 64;

/** Counters values offset. */
static constexpr uint32_t values_offset = 128;

/** Dump buffer memory. */
static std::array<uint8_t, 1024> mapping_data{};

/** Sample metadata address. */
static const uint8_t *metadata_addr = mapping_data.data() + metadata_offset;

/** Sample access structure. */
static ioctl::kinstr_prfcnt::sample_access sample_access = {sample_nr, {metadata_offset}};

/** Expected block metadata. */
static const hwcnt::block_metadata block_metadata = {
    hwcnt::block_type::memory, 42, hwcnt::prfcnt_set::secondary, {}, mapping_data.data() + values_offset,
};

/** Block metadata item. */
static ioctl::kinstr_prfcnt::metadata_item block_item = test::metadata_item::block(
    ioctl::kinstr_prfcnt::block_type::fe, 0, ioctl::kinstr_prfcnt::prfcnt_set::primary, values_offset);

} // namespace test

namespace mock {
/** Mock metadata parser. */
class metadata_parser {
  public:
    /** Mock metadata parser configuration. */
    class config {
      public:
        /** Constructor.
         *
         * @param ec    Error code to return from `metadata_parser::parse_sample` function.
         */
        config(std::error_code ec) {
            setup = true;
            metadata_parser::ec = ec;
            metadata_parser::block_parsing_done_v = false;
        }

        /** Notify block parsing done.
         *
         * `metadata_parser::parse_block` will return false after calling this function.
         */
        void block_parsing_done() const { block_parsing_done_v = true; }

        /** Destructor. */
        ~config() { setup = false; };
    };

    metadata_parser(hwcnt::sample_metadata &metadata, const hwcnt::block_extents &extents)
        : metadata_(metadata) {
        REQUIRE(setup);

        CHECK(extents == test::extents);
    }

    template <typename iterator_t, typename block_index_remap_t>
    std::error_code parse_sample(iterator_t begin, const block_index_remap_t &) {
        REQUIRE(setup);

        CHECK(static_cast<const void *>(&*begin) == static_cast<const void *>(test::metadata_addr));

        if (!ec)
            metadata_ = test::sample_metadata;

        return ec;
    }

    template <typename iterator_t, typename block_index_remap_t>
    static auto parse_block(iterator_t &begin, const uint8_t *mapping, const block_index_remap_t &) {
        REQUIRE(setup);
        CHECK(mapping == test::mapping_data.data());

        hwcnt::block_metadata metadata = test::block_metadata;

        if (!block_parsing_done_v)
            begin++;

        return std::make_pair(!block_parsing_done_v, metadata);
    }

  private:
    hwcnt::sample_metadata &metadata_;

    /** True if setup was done. */
    static bool setup;
    /** True if block parsing is done. */
    static bool block_parsing_done_v;
    /** Error code to return from `parse_sample`. */
    static std::error_code ec;
};

std::error_code metadata_parser::ec = {};
bool metadata_parser::setup = false;
bool metadata_parser::block_parsing_done_v = false;

} // namespace mock

SCENARIO("device::hwcnt::sampler::kinstr_prfcnt::backend", "[unit]") {
    using backend_type = hwcnt::sampler::kinstr_prfcnt::backend<mock::syscall::iface, mock::metadata_parser>;
    using backend_args_type = backend_type::args_type;

    mock::syscall::iface iface;

    bool close_called = false;

    iface.close_fn = [&](int fd) {
        CHECK(fd == test::kinstr_prfcnt_fd);
        close_called = true;
        return std::error_code{};
    };

    iface.munmap_fn = [&](void *addr, size_t len) {
        CHECK(addr == test::mapping_data.data());
        CHECK(len == test::mapping_data.size());
        return std::error_code{};
    };

    bool poll_called = false;
    iface.poll_fn = [&](struct pollfd *fds, nfds_t nfds, int timeout) {
        REQUIRE(nfds == 1);
        CHECK(fds[0].fd == test::kinstr_prfcnt_fd);
        CHECK(timeout == -1);
        poll_called = true;
        return std::make_pair(std::error_code{}, 1);
    };

    using control_cmd_code_type = ioctl::kinstr_prfcnt::control_cmd::control_cmd_code;
    control_cmd_code_type command_code{};

    bool command_called = false;
    bool get_sample_called = false;
    bool put_sample_called = false;

    iface.ioctl_fn = [&](int fd, unsigned long command, void *argp) {
        CHECK(fd == test::kinstr_prfcnt_fd);
        REQUIRE(argp);

        namespace iotcl = hwcpipe::device::ioctl;
        switch (command) {
        case ioctl::kinstr_prfcnt::command::issue_command: {
            CHECK(!command_called);

            auto command = static_cast<ioctl::kinstr_prfcnt::control_cmd *>(argp);
            command_code = command->cmd;

            if (command->cmd == control_cmd_code_type::discard)
                CHECK(command->user_data == 0);
            else
                CHECK(command->user_data == test::user_data);

            command_called = true;
        } break;
        case ioctl::kinstr_prfcnt::command::get_sample:
            CHECK(poll_called);
            CHECK(!get_sample_called);

            *static_cast<ioctl::kinstr_prfcnt::sample_access *>(argp) = test::sample_access;

            get_sample_called = true;
            break;
        case ioctl::kinstr_prfcnt::command::put_sample:
            CHECK(!put_sample_called);
            CHECK(*static_cast<ioctl::kinstr_prfcnt::sample_access *>(argp) == test::sample_access);
            put_sample_called = true;
            break;
        }

        return std::make_pair(std::error_code{}, 0);
    };

    backend_args_type::memory_type memory{test::mapping_data.data(), test::mapping_data.size(), iface};
    backend_args_type backend_args{};

    backend_args.base_args.fd.reset(test::kinstr_prfcnt_fd);
    backend_args.base_args.period_ns = GENERATE(0, 1000);
    backend_args.base_args.features_v = {};
    backend_args.base_args.extents = test::extents;
    backend_args.base_args.memory = std::move(memory);

    backend_args.metadata_item_size = test::metadata_item_size;

    GIVEN("backend instance") {
        backend_type backend{std::move(backend_args), iface};

        WHEN("start() is called") {
            auto ec = backend.start(test::user_data);
            CHECK(!ec);

            THEN("command is issued") {
                CHECK(command_called);
                CHECK(command_code == control_cmd_code_type::start);
            }
        }
        WHEN("stop() is called") {
            auto ec = backend.stop(test::user_data);
            CHECK(!ec);

            THEN("command is issued") {
                CHECK(command_called);
                CHECK(command_code == control_cmd_code_type::stop);
            }
        }
        WHEN("request_sample() is called") {
            auto ec = backend.request_sample(test::user_data);
            CHECK(!ec);

            THEN("command is issued") {
                CHECK(command_called);
                CHECK(command_code == control_cmd_code_type::sample_sync);
            }
        }
        WHEN("discard() is called") {
            auto ec = backend.discard();
            CHECK(!ec);

            THEN("command is issued") {
                CHECK(command_called);
                CHECK(command_code == control_cmd_code_type::discard);
            }
        }
        WHEN("get_sample() is called, but metadata.parse() fails") {
            hwcnt::sample_metadata sample_metadata{};
            hwcnt::sample_handle sample_handle{};

            const auto expected_error = std::make_error_code(std::errc::invalid_argument);

            mock::metadata_parser::config config(expected_error);

            auto ec = backend.get_sample(sample_metadata, sample_handle);
            CHECK(get_sample_called);
            CHECK(put_sample_called);
            CHECK(expected_error == ec);
        }
        WHEN("get_sample() is called") {
            hwcnt::sample_metadata sample_metadata{};
            hwcnt::sample_handle sample_handle{};

            mock::metadata_parser::config config(std::error_code{});

            auto ec = backend.get_sample(sample_metadata, sample_handle);
            CHECK(get_sample_called);

            REQUIRE(!ec);

            THEN("sample_handle stores sample_access structure") {
                CHECK(sample_handle.get<ioctl::kinstr_prfcnt::sample_access>() == test::sample_access);
            }

            THEN("metadata is set") { CHECK(sample_metadata == test::sample_metadata); }
        }
        WHEN("put_sample() is called") {
            hwcnt::sample_handle sample_handle{};
            sample_handle.get<ioctl::kinstr_prfcnt::sample_access>() = test::sample_access;

            auto ec = backend.put_sample(sample_handle);
            CHECK(put_sample_called);
            REQUIRE(!ec);
        }
        WHEN("next() is called") {
            hwcnt::sample_handle sample_handle{};
            sample_handle.get<ioctl::kinstr_prfcnt::sample_access>() = test::sample_access;

            hwcnt::block_metadata block_metadata{};
            hwcnt::block_handle block_handle{};

            static constexpr size_t num_blocks = 10;
            size_t block_idx = 0;

            const mock::metadata_parser::config config(std::error_code{});

            THEN("blocks can be iterated") {
                for (; block_idx != num_blocks; ++block_idx) {
                    block_metadata = {};
                    auto result = backend.next(sample_handle, block_metadata, block_handle);
                    REQUIRE(result);

                    auto block_addr = test::metadata_addr + test::metadata_item_size * (block_idx + 1);
                    CHECK(block_handle.get<const uint8_t *>() == block_addr);

                    CHECK(block_metadata == test::block_metadata);
                }
                AND_WHEN("next() is called again") {
                    config.block_parsing_done();
                    auto result = backend.next(sample_handle, block_metadata, block_handle);
                    THEN("it returns false") { CHECK(!result); }
                }
            }
        }
    }
    CHECK(close_called);
}
