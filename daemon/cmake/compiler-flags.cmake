# Copyright (C) 2021-2022 by Arm Limited. All rights reserved.

# Configure target flags
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GATORD_C_CXX_FLAGS}")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GATORD_C_CXX_FLAGS} -fexceptions")

IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # Validate version
    IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "7.0.0")
        MESSAGE(FATAL_ERROR "Invalid G++ version ${CMAKE_CXX_COMPILER_VERSION} (expected minimum is 7.0.0)")
    ENDIF()

    IF(CMAKE_ENABLE_LTO AND ("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "8.0.0"))
        MESSAGE(FATAL_ERROR "LTO unsupported for G++ versions less than 8.0.0, please disable LTO and reconfigure")
    ENDIF()
ELSEIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # Validate version
    IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "5.0.0")
        MESSAGE(FATAL_ERROR "Invalid clang++ version ${CMAKE_CXX_COMPILER_VERSION} (expected minimum is 5.0.0)")
    ENDIF()
ELSE()
    MESSAGE(FATAL_ERROR "Unknown compiler type '${CMAKE_CXX_COMPILER_ID}'")
ENDIF()

# When ccache basdir is set, remap paths to allow result sharing between builds
IF(DEFINED ENV{CCACHE_BASEDIR})
    IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "8.0.0")
            SET(CCACHE_PATH_MAP_OPT "-fdebug-prefix-map=$ENV{CCACHE_BASEDIR}=.")
        ELSE()
            SET(CCACHE_PATH_MAP_OPT "-ffile-prefix-map=$ENV{CCACHE_BASEDIR}=.")
        ENDIF()
    ELSEIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        IF("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "10.0.0")
            SET(CCACHE_PATH_MAP_OPT "-fdebug-prefix-map=$ENV{CCACHE_BASEDIR}=.")
        ELSE()
            SET(CCACHE_PATH_MAP_OPT "-ffile-prefix-map=$ENV{CCACHE_BASEDIR}=.")
        ENDIF()
    ELSE()
        SET(CCACHE_PATH_MAP_OPT "")
    ENDIF()

    MESSAGE(STATUS "CCACHE_PATH_MAP_OPT = ${CCACHE_PATH_MAP_OPT}")

    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CCACHE_PATH_MAP_OPT}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CCACHE_PATH_MAP_OPT}")
ENDIF()

# ###
# Configure build target flags
# ###
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -ggdb3 -gdwarf-3 -D_DEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O3 -g")
    set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -ggdb3 -gdwarf-3 -D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -Os -DNDEBUG")
endif()

# ###
# Add strict warning settings for C++
# ###
IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(GCC_STRICT_WARNING_FLAGS_CXX "-Wall -Wextra -Wno-shadow -Wno-psabi")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_STRICT_WARNING_FLAGS_CXX}")
ELSEIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CLANG_STRICT_WARNING_FLAGS_CXX "-Wall -Wextra -Wno-shadow -Wdocumentation")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CLANG_STRICT_WARNING_FLAGS_CXX}")
ENDIF()
