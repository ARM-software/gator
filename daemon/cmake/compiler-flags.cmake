# Copyright (C) 2021 by Arm Limited. All rights reserved.

# Configure target flags
SET(CMAKE_C_FLAGS           "${CMAKE_C_FLAGS} ${GATORD_C_CXX_FLAGS}")
SET(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} ${GATORD_C_CXX_FLAGS} -fexceptions")

IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # Validate version
    IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "7.0.0")
        MESSAGE(FATAL_ERROR "Invalid G++ version ${CMAKE_CXX_COMPILER_VERSION} (expected minimum is 7.0.0)")
    ENDIF()
ELSEIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # Validate version
    IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "5.0.0")
        MESSAGE(FATAL_ERROR "Invalid clang++ version ${CMAKE_CXX_COMPILER_VERSION} (expected minimum is 5.0.0)")
    ENDIF()
ELSE()
    MESSAGE(FATAL_ERROR "Unknown compiler type '${CMAKE_CXX_COMPILER_ID}'")
ENDIF()

####
#   Configure build target flags
####
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_C_FLAGS_DEBUG                 "${CMAKE_C_FLAGS_DEBUG} -g -ggdb3 -gdwarf-3 -D_DEBUG")
    set(CMAKE_C_FLAGS_RELEASE               "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO        "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O3 -g")
    set(CMAKE_C_FLAGS_MINSIZEREL            "${CMAKE_C_FLAGS_MINSIZEREL} -Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG               "${CMAKE_CXX_FLAGS_DEBUG} -g -ggdb3 -gdwarf-3 -D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE             "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO      "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL          "${CMAKE_CXX_FLAGS_MINSIZEREL} -Os -DNDEBUG")
endif()

####
#   Add strict warning settings for C++
####
IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(GCC_STRICT_WARNING_FLAGS_CXX    "-Wall -Wextra -Wno-shadow -Wno-psabi")
    set(CMAKE_CXX_FLAGS                 "${CMAKE_CXX_FLAGS} ${GCC_STRICT_WARNING_FLAGS_CXX}")
ELSEIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CLANG_STRICT_WARNING_FLAGS_CXX  "-Wall -Wextra -Wno-shadow -Wdocumentation")
    set(CMAKE_CXX_FLAGS                 "${CMAKE_CXX_FLAGS} ${CLANG_STRICT_WARNING_FLAGS_CXX}")
ENDIF()
