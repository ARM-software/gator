# Copyright (C) 2021 by Arm Limited. All rights reserved.

FIND_PROGRAM(CMAKE_ADDR2LINE    NAMES   "addr2line"             REQUIRED)
FIND_PROGRAM(CMAKE_AR           NAMES   "llvm-ar" "ar"          REQUIRED)
FIND_PROGRAM(CMAKE_LINKER       NAMES   "ld.lld" "ld.gold" "ld" REQUIRED)
FIND_PROGRAM(CMAKE_NM           NAMES   "llvm-nm" "nm"          REQUIRED)
FIND_PROGRAM(CMAKE_OBJCOPY      NAMES   "objcopy"               REQUIRED)
FIND_PROGRAM(CMAKE_OBJDUMP      NAMES   "objdump"               REQUIRED)
FIND_PROGRAM(CMAKE_RANLIB       NAMES   "llvm-ranlib" "ranlib"  REQUIRED)
FIND_PROGRAM(CMAKE_READELF      NAMES   "readelf"               REQUIRED)
FIND_PROGRAM(CMAKE_STRIP        NAMES   "strip"                 REQUIRED)

FIND_PROGRAM(CMAKE_C_COMPILER           NAMES   "clang"         REQUIRED)
FIND_PROGRAM(CMAKE_C_COMPILER_AR        NAMES   "llvm-ar")
FIND_PROGRAM(CMAKE_C_COMPILER_NM        NAMES   "llvm-nm")
FIND_PROGRAM(CMAKE_C_COMPILER_RANLIB    NAMES   "llvm-ranlib")
FIND_PROGRAM(CMAKE_CXX_COMPILER         NAMES   "clang++"       REQUIRED)
FIND_PROGRAM(CMAKE_CXX_COMPILER_AR      NAMES   "llvm-ar")
FIND_PROGRAM(CMAKE_CXX_COMPILER_NM      NAMES   "llvm-nm")
FIND_PROGRAM(CMAKE_CXX_COMPILER_RANLIB  NAMES   "llvm-ranlib")

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

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.toolchain.cmake")
