# Copyright (C) 2024-2025 by Arm Limited (or its affiliates). All rights reserved.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../../cmake/aarch64-gcc7.toolchain.cmake)

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
