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

#include <device/hwcnt/sampler/mapped_memory.hpp>
#include <device/mock/syscall/iface.hpp>

#include <array>

#include <sys/mman.h>

SCENARIO("device::hwcnt::sampler::mapped_memory", "[unit]") {
    using hwcpipe::device::hwcnt::sampler::mapped_memory;
    using test_mapped_memory = mapped_memory<mock::syscall::iface>;

    GIVEN("mapped_memory class and mocked syscall::iface") {
        mock::syscall::iface iface;
        std::error_code ec;

        const int mapping_fd = 42;
        std::array<char, 1024> data{};

        struct call_stats {
            uint32_t mmap_called{};
            uint32_t munmap_called{};
        } stats;

        iface.mmap_fn = [&](void *addr, size_t len, int prot, int flags, int fd, off_t off) {
            CHECK(addr == nullptr);
            CHECK(len == data.size());
            CHECK(prot == PROT_READ);
            CHECK(flags == MAP_PRIVATE);
            CHECK(fd == mapping_fd);
            CHECK(off == 0);

            ++stats.mmap_called;

            return std::make_pair(std::error_code{}, data.data());
        };

        iface.munmap_fn = [&](void *addr, size_t len) {
            CHECK(addr == data.data());
            CHECK(len == data.size());

            ++stats.munmap_called;

            return std::error_code{};
        };

        WHEN("default constructor is called")
        THEN("no system calls are called") { test_mapped_memory m{}; }

        WHEN("empty mapping is created") {
            { test_mapped_memory m{}; }
            THEN("no syscalls are called") {
                CHECK(0 == stats.mmap_called);
                CHECK(0 == stats.munmap_called);
            }
        }

        WHEN("mapping is created from memory") {
            { test_mapped_memory m{data.data(), data.size(), iface}; }
            THEN("mmap is not called and munmap is called once") {
                CHECK(0 == stats.mmap_called);
                CHECK(1 == stats.munmap_called);
            }
        }

        WHEN("mapping is created") {
            {
                std::error_code ec;
                test_mapped_memory m{mapping_fd, data.size(), ec, iface};
            }
            THEN("mmap and munmap are called once") {
                CHECK(1 == stats.mmap_called);
                CHECK(1 == stats.munmap_called);
            }
        }

        WHEN("mapping is created and then move-constructed") {
            {
                std::error_code ec;
                test_mapped_memory m0{mapping_fd, data.size(), ec, iface};
                test_mapped_memory m1 = std::move(m0);
            }
            THEN("mmap and munmap are called once") {
                CHECK(1 == stats.mmap_called);
                CHECK(1 == stats.munmap_called);
            }
        }

        WHEN("mapping is created and then move-assigned") {
            {
                std::error_code ec;
                test_mapped_memory m0{mapping_fd, data.size(), ec, iface};
                test_mapped_memory m1{};
                m1 = std::move(m0);
            }
            THEN("mmap and munmap are called once") {
                CHECK(1 == stats.mmap_called);
                CHECK(1 == stats.munmap_called);
            }
        }
    }

    GIVEN("mocked syscall::iface that fails") {
        mock::syscall::iface iface;
        std::error_code ec;

        bool mmap_called = false;
        iface.mmap_fn = [&mmap_called](void * /* addr */, size_t /* len */, int /* prot */, int /* flags */,
                                       int /* fd */, off_t /* off */) {
            mmap_called = true;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            return std::make_pair(std::make_error_code(std::errc::invalid_argument), MAP_FAILED);
        };

        WHEN("mapping is created") {
            const int mapping_fd = 42;
            constexpr size_t any_size = 1024;

            test_mapped_memory m{mapping_fd, any_size, ec, iface};

            THEN("mmap is called, but munmap is not") { CHECK(mmap_called); }
        }
    }
}
