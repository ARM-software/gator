# Copyright (C) 2021-2023 by Arm Limited. All rights reserved.

# Tell CMake that we're building for a Linux target
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_VERSION 1)
SET(CMAKE_SYSTEM_PROCESSOR aarch64)
SET(CMAKE_SYSTEM_FLOAT_ABI hard)

# specify the cross compiler aarch64-linux-musl-
SET(CROSS_COMPILE "aarch64-linux-musl-" CACHE STRING "")

# Start with any user specified flags
SET(CMAKE_C_FLAGS "$CACHE{CMAKE_C_FLAGS}")
SET(CMAKE_CXX_FLAGS "$CACHE{CMAKE_CXX_FLAGS}")
SET(CMAKE_EXE_LINKER_FLAGS "$CACHE{CMAKE_EXE_LINKER_FLAGS}")
SET(CMAKE_MODULE_LINKER_FLAGS "$CACHE{CMAKE_MODULE_LINKER_FLAGS}")
SET(CMAKE_SHARED_LINKER_FLAGS "$CACHE{CMAKE_SHARED_LINKER_FLAGS}")

IF(DEFINED SYSROOT)
    SET(CMAKE_SYSROOT ${SYSROOT})
ENDIF()

# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Find the commands
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/xcompiler.toolchain.cmake")

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.cmake")
