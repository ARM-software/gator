# Copyright (C) 2021 by Arm Limited. All rights reserved.

# No cross compiler
SET(CROSS_COMPILE               ""
                CACHE STRING "")

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

# Find the commands
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/xcompiler.toolchain.cmake")

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.toolchain.cmake")
