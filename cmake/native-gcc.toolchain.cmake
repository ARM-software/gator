# Copyright (C) 2021-2023 by Arm Limited. All rights reserved.

# No cross compiler
SET(CROSS_COMPILE "" CACHE STRING "")

# Start with any user specified flags
SET(CMAKE_C_FLAGS               "$CACHE{CMAKE_C_FLAGS}")
SET(CMAKE_CXX_FLAGS             "$CACHE{CMAKE_CXX_FLAGS}")
SET(CMAKE_EXE_LINKER_FLAGS      "$CACHE{CMAKE_EXE_LINKER_FLAGS}")
SET(CMAKE_MODULE_LINKER_FLAGS   "$CACHE{CMAKE_MODULE_LINKER_FLAGS}")
SET(CMAKE_SHARED_LINKER_FLAGS   "$CACHE{CMAKE_SHARED_LINKER_FLAGS}")

# Find the commands
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/xcompiler.toolchain.cmake")

# LTO options
INCLUDE("${CMAKE_CURRENT_LIST_DIR}/lto.cmake")
