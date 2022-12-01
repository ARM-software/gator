# Copyright (C) 2021 by Arm Limited. All rights reserved.

# Tell CMake that we're building for a Linux target
SET(CMAKE_SYSTEM_NAME           Linux)
SET(CMAKE_SYSTEM_VERSION        1)
SET(CMAKE_SYSTEM_PROCESSOR      aarch64)
SET(CMAKE_SYSTEM_FLOAT_ABI      hard)

# specify the cross compiler
SET(CROSS_COMPILE               "/tools/gcc7-aarch64-linux-gnu/bin/aarch64-linux-gnu-"  CACHE STRING "")

FIND_PROGRAM(CMAKE_ADDR2LINE    NAMES   "${CROSS_COMPILE}addr2line"     REQUIRED)
FIND_PROGRAM(CMAKE_AR           NAMES   "${CROSS_COMPILE}gcc-ar"
                                        "${CROSS_COMPILE}ar"            REQUIRED)
FIND_PROGRAM(CMAKE_LINKER       NAMES   "${CROSS_COMPILE}ld.gold"
                                        "${CROSS_COMPILE}ld"            REQUIRED)
FIND_PROGRAM(CMAKE_NM           NAMES   "${CROSS_COMPILE}gcc-nm"
                                        "${CROSS_COMPILE}nm"            REQUIRED)
FIND_PROGRAM(CMAKE_OBJCOPY      NAMES   "${CROSS_COMPILE}objcopy"       REQUIRED)
FIND_PROGRAM(CMAKE_OBJDUMP      NAMES   "${CROSS_COMPILE}objdump"       REQUIRED)
FIND_PROGRAM(CMAKE_RANLIB       NAMES   "${CROSS_COMPILE}gcc-ranlib"
                                        "${CROSS_COMPILE}ranlib"        REQUIRED)
FIND_PROGRAM(CMAKE_READELF      NAMES   "${CROSS_COMPILE}readelf"       REQUIRED)
FIND_PROGRAM(CMAKE_STRIP        NAMES   "${CROSS_COMPILE}strip"         REQUIRED)

FIND_PROGRAM(CMAKE_C_COMPILER           NAMES   "${CROSS_COMPILE}gcc"   REQUIRED)
FIND_PROGRAM(CMAKE_C_COMPILER_AR        NAMES   "${CROSS_COMPILE}gcc-ar")
FIND_PROGRAM(CMAKE_C_COMPILER_NM        NAMES   "${CROSS_COMPILE}gcc-nm")
FIND_PROGRAM(CMAKE_C_COMPILER_RANLIB    NAMES   "${CROSS_COMPILE}gcc-ranlib")
FIND_PROGRAM(CMAKE_CXX_COMPILER         NAMES   "${CROSS_COMPILE}g++"   REQUIRED)
FIND_PROGRAM(CMAKE_CXX_COMPILER_AR      NAMES   "${CROSS_COMPILE}gcc-ar")
FIND_PROGRAM(CMAKE_CXX_COMPILER_NM      NAMES   "${CROSS_COMPILE}gcc-nm")
FIND_PROGRAM(CMAKE_CXX_COMPILER_RANLIB  NAMES   "${CROSS_COMPILE}gcc-ranlib")

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
