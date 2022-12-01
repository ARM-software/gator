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

#include <catch2/catch.hpp>

#include <functional>
#include <system_error>

#include <poll.h>

namespace mock {
namespace syscall {
/**
 * Syscall interface mock.
 *
 * Mocks @ref hwcpipe::device::syscall::iface type.
 * By default, all syscall functions are failing when called.
 * To override this behavior the user should bind a new functor:
 * @code
 * mock::syscall::iface iface;
 *
 * const char *device = "/a/b/c/d.txt";
 * // The following line will result into a test failure
 * // iface.open(device , O_RDONLY);
 *
 * iface.open_fn = [&](const char *path, int mode) {
 *     CHECK(path == device);
 *     CHECK(mode == O_RDONLY);
 * }
 *
 * // The below line passes now.
 * iface.open(device , O_RDONLY);
 * @endcode
 */
class iface {
  public:
    std::pair<std::error_code, int> open(const char *name, int oflags) {
        REQUIRE(open_fn);
        return open_fn(name, oflags);
    }

    std::pair<std::error_code, bool> is_char_device(int fd) {
        REQUIRE(is_char_device_fn);
        return is_char_device_fn(fd);
    }

    std::error_code close(int fd) {
        REQUIRE(close_fn);
        return close_fn(fd);
    }

    std::pair<std::error_code, void *> mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
        REQUIRE(mmap_fn);
        return mmap_fn(addr, len, prot, flags, fd, off);
    }

    std::error_code munmap(void *addr, size_t len) {
        REQUIRE(munmap_fn);
        return munmap_fn(addr, len);
    }

    std::pair<std::error_code, int> ioctl(int fd, unsigned long command) {
        REQUIRE(ioctl_fn);
        return ioctl_fn(fd, command, nullptr);
    };

    template <typename value_t>
    std::pair<std::error_code, int> ioctl(int fd, unsigned long command, value_t val) {
        REQUIRE(ioctl_fn);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto val_ptr = reinterpret_cast<void *>(static_cast<uintptr_t>(val));

        return ioctl_fn(fd, command, val_ptr);
    }

    template <typename value_t>
    std::pair<std::error_code, int> ioctl(int fd, unsigned long command, value_t *val) {
        REQUIRE(ioctl_fn);
        return ioctl_fn(fd, command, val);
    }

    std::pair<std::error_code, int> poll(struct pollfd *fds, nfds_t nfds, int timeout) {
        REQUIRE(poll_fn);
        return poll_fn(fds, nfds, timeout);
    }

    std::function<std::pair<std::error_code, int>(const char *, int)> open_fn;
    std::function<std::pair<std::error_code, bool>(int)> is_char_device_fn;
    std::function<std::error_code(int)> close_fn;
    std::function<std::pair<std::error_code, void *>(void *, size_t, int, int, int, off_t)> mmap_fn;
    std::function<std::error_code(void *addr, size_t len)> munmap_fn;
    std::function<std::pair<std::error_code, int>(int fd, unsigned long command, void *argp)> ioctl_fn;
    std::function<std::pair<std::error_code, int>(struct pollfd *fds, nfds_t nfds, int timeout)> poll_fn;
};

} // namespace syscall
} // namespace mock
