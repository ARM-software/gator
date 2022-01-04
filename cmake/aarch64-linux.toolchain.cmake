# Copyright (C) 2021 by Arm Limited. All rights reserved.

# Tell CMake that we're building for a Linux target
SET(CMAKE_SYSTEM_NAME           Linux)
SET(CMAKE_SYSTEM_VERSION        1)
SET(CMAKE_SYSTEM_PROCESSOR      aarch64)
SET(CMAKE_SYSTEM_FLOAT_ABI      hard)

# specify the cross compiler as aarch64-linux-gnu- or aarch64-none-linux-gnu-
IF(NOT DEFINED ENV{CROSS_COMPILE})
    FIND_PROGRAM(AARCH64_LINUX_GNU_GXX      "aarch64-linux-gnu-g++")
    FIND_PROGRAM(AARCH64_NONE_LINUX_GNU_GXX "aarch64-none-linux-gnu-g++")

    IF(AARCH64_LINUX_GNU_GXX AND AARCH64_NONE_LINUX_GNU_GXX)
        MESSAGE(WARNING "Found both ${AARCH64_LINUX_GNU_GXX} and ${AARCH64_NONE_LINUX_GNU_GXX}, using ${AARCH64_LINUX_GNU_GXX}. Set the CROSS_COMPILE environment variable to override with the specific prefix if preferred.")
    ENDIF()

    IF(AARCH64_LINUX_GNU_GXX)
        SET(CROSS_COMPILE       "aarch64-linux-gnu-"        CACHE STRING "" FORCE)
    ELSEIF(AARCH64_NONE_LINUX_GNU_GXX)
        SET(CROSS_COMPILE       "aarch64-none-linux-gnu-"   CACHE STRING "" FORCE)
    ELSE()
        MESSAGE(FATAL_ERROR "Could not determine compiler prefix. Set the CROSS_COMPILE environment variable or ensure the compiler is in PATH")
    ENDIF()
ELSE()
    SET(CROSS_COMPILE           "$ENV{CROSS_COMPILE}"   CACHE STRING "" FORCE)
ENDIF()

SET(CMAKE_C_FLAGS               ""
                                CACHE STRING "Default GCC compiler flags")
SET(CMAKE_CXX_FLAGS             ""
                                CACHE STRING "Default G++ compiler flags")
SET(CMAKE_EXE_LINKER_FLAGS      ""
                                CACHE STRING "Default exe linker flags")
SET(CMAKE_MODULE_LINKER_FLAGS   ""
                                CACHE STRING "Default module linker flags")
SET(CMAKE_SHARED_LINKER_FLAGS   ""
                                CACHE STRING "Default shared linker flags")

IF(DEFINED SYSROOT)
    SET(CMAKE_SYSROOT           ${SYSROOT})
ENDIF()

# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Find the commands
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/xcompiler.toolchain.cmake")

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.toolchain.cmake")
