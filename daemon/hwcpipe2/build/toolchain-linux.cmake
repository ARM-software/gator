#
# Copyright (c) 2021-2022 ARM Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

set(HWCPIPE_TOOLCHAIN
    "gcc"
    CACHE STRING "Which compiler toolchain to use: gcc or clang"
)
set(HWCPIPE_TARGET_ARCH
    "x64"
    CACHE STRING "Target architecture: x86, x64, arm, arm64"
)
set(HWCPIPE_TOOLCHAIN_VERSION
    ""
    CACHE STRING "Toolchain version, e.g. if set to 8 gcc-8 is used"
)
set(HWCPIPE_USE_CCACHE
    OFF
    CACHE BOOL "Use ccache to cache object files."
)

# Enable IN_LIST operator in if()
cmake_policy(
    SET
    CMP0057
    NEW
)

if(HWCPIPE_USE_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

set(TOOLCHAIN_VERSION ${HWCPIPE_TOOLCHAIN_VERSION})
if(TOOLCHAIN_VERSION)
    # Fix-up HWCPIPE_TOOLCHAIN_VERSION to add leading dash, s.t.
    # gcc${TOOLCHAIN_VERSION} is evaluated to e.g. gcc-8
    set(TOOLCHAIN_VERSION "-${TOOLCHAIN_VERSION}")
endif()

set(SUPPORTED_TARGET_ARCH
    "x86"
    "x64"
    "arm"
    "arm64"
)
if(NOT
   HWCPIPE_TARGET_ARCH
   IN_LIST
   SUPPORTED_TARGET_ARCH
)
    message(FATAL "Unknown target architecture ${HWCPIPE_TARGET_ARCH}")
endif()

set(SYSTEM_PROCESSOR_x86 "x86")
set(SYSTEM_PROCESSOR_x64 "x64")
set(SYSTEM_PROCESSOR_arm "armv7-a")
set(SYSTEM_PROCESSOR_arm64 "aarch64")

set(CMAKE_SYSTEM_PROCESSOR ${SYSTEM_PROCESSOR_${HWCPIPE_TARGET_ARCH}})

# Setting this variable will also set CMAKE_CROSSCOMPILING to TRUE. But we must
# be always crosscompiling (even for x86), otherwise cmake sets
# CMAKE_SYSTEM_PROCESSOR to ${CMAKE_HOST_SYSTEM_PROCESSOR} unconditionally.
set(CMAKE_SYSTEM_NAME "Linux")

if(HWCPIPE_TARGET_ARCH
   STREQUAL
   "x86"
)
    set(CMAKE_LD_FLAGS_INIT "-m32")
    set(CMAKE_C_FLAGS_INIT "-m32")
    set(CMAKE_CXX_FLAGS_INIT "-m32")
endif()

if(HWCPIPE_TOOLCHAIN
   STREQUAL
   "gcc"
)
    set(TOOL_PREFIX_x86 "")
    set(TOOL_PREFIX_x64 "")
    set(TOOL_PREFIX_arm "arm-linux-gnueabihf-")
    set(TOOL_PREFIX_arm64 "aarch64-linux-gnu-")
    set(TOOL_PREFIX "${TOOL_PREFIX_${HWCPIPE_TARGET_ARCH}}")

    set(CMAKE_C_COMPILER "${TOOL_PREFIX}gcc${TOOLCHAIN_VERSION}")
    set(CMAKE_CXX_COMPILER "${TOOL_PREFIX}g++${TOOLCHAIN_VERSION}")
elseif(
    HWCPIPE_TOOLCHAIN
    STREQUAL
    "clang"
)
    set(TRIPLE_x86 "")
    set(TRIPLE_x64 "")
    set(TRIPLE_arm "arm-linux-gnueabihf")
    set(TRIPLE_arm64 "aarch64-linux-gnu")
    set(TRIPLE ${TRIPLE_${HWCPIPE_TARGET_ARCH}})

    set(CMAKE_C_COMPILER_TARGET ${TRIPLE})
    set(CMAKE_CXX_COMPILER_TARGET ${TRIPLE})
    set(CMAKE_C_COMPILER "clang${TOOLCHAIN_VERSION}")
    set(CMAKE_CXX_COMPILER "clang++${TOOLCHAIN_VERSION}")

    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=lld")

    # GCC cross compilers for Ubuntu have wrong configuration. They configured
    # --with-sysroot=/ and --includedir=$SYSROOT That is why clang cannot read
    # sysroot from gcc. Here we set the sysroot argument manually.
    if(TRIPLE AND (EXISTS "/usr/${TRIPLE}"))
        set(CMAKE_SYSROOT_COMPILE "/usr/${TRIPLE}")
    endif()

else()
    message(FATAL "Toolchain '${HWCPIPE_TOOLCHAIN}' is not known")
endif()
