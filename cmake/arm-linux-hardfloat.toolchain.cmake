# Copyright (C) 2010-2023 by Arm Limited. All rights reserved.

# Tell CMake that we're building for a Linux target
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_VERSION 1)
SET(CMAKE_SYSTEM_PROCESSOR armv7)
SET(CMAKE_SYSTEM_FLOAT_ABI hard)

# specify the cross compiler as arm-linux-gnueabihf- or arm-none-linux-gnueabihf-
IF(NOT DEFINED ENV{CROSS_COMPILE})
    FIND_PROGRAM(ARM_LINUX_GNU_GXX "arm-linux-gnueabihf-g++")
    FIND_PROGRAM(ARM_NONE_LINUX_GNU_GXX "arm-none-linux-gnueabihf-g++")

    IF(ARM_LINUX_GNU_GXX AND ARM_NONE_LINUX_GNU_GXX)
        MESSAGE(WARNING "Found both ${ARM_LINUX_GNU_GXX} and ${ARM_NONE_LINUX_GNU_GXX}, using ${ARM_LINUX_GNU_GXX}. Set the CROSS_COMPILE environment variable to override with the specific prefix if preferred.")
    ENDIF()

    IF(ARM_LINUX_GNU_GXX)
        SET(CROSS_COMPILE "arm-linux-gnueabihf-" CACHE STRING "" FORCE)
    ELSEIF(ARM_NONE_LINUX_GNU_GXX)
        SET(CROSS_COMPILE "arm-none-linux-gnueabihf-" CACHE STRING "" FORCE)
    ELSE()
        MESSAGE(FATAL_ERROR "Could not determine compiler prefix. Set the CROSS_COMPILE environment variable or ensure the compiler is in PATH")
    ENDIF()
ELSE()
    SET(CROSS_COMPILE "$ENV{CROSS_COMPILE}" CACHE STRING "" FORCE)
ENDIF()

# Start with any user specified flags
SET(CMAKE_C_FLAGS "$CACHE{CMAKE_C_FLAGS} -mfloat-abi=hard")
SET(CMAKE_CXX_FLAGS "$CACHE{CMAKE_CXX_FLAGS} -mfloat-abi=hard")
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
