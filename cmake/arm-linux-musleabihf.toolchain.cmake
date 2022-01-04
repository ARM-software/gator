# Copyright (C) 2010-2021 by Arm Limited. All rights reserved.

# Tell CMake that we're building for a Linux target
SET(CMAKE_SYSTEM_NAME           Linux)
SET(CMAKE_SYSTEM_VERSION        1)
SET(CMAKE_SYSTEM_PROCESSOR      armv7)
SET(CMAKE_SYSTEM_FLOAT_ABI      hard)

# specify the cross compiler armv7l-linux-musleabihf-
SET(CROSS_COMPILE               "armv7l-linux-musleabihf-"      CACHE STRING "")

SET(CMAKE_C_FLAGS               "-mfloat-abi=hard"
                                CACHE STRING "Default GCC compiler flags")
SET(CMAKE_CXX_FLAGS             "-mfloat-abi=hard"
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

SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


# Find the commands
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/xcompiler.toolchain.cmake")

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.toolchain.cmake")
