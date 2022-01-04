# Copyright (C) 2010-2020 by Arm Limited. All rights reserved.

# Convert the command line defined CMAKE_BUILD_TYPE into a CMake variable
IF((DEFINED CMAKE_BUILD_TYPE) AND (("${CMAKE_BUILD_TYPE}" STREQUAL "Debug") OR
                                   ("${CMAKE_BUILD_TYPE}" STREQUAL "Release") OR
                                   ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo") OR
                                   ("${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")))
   SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel.")
ELSE()
   # If undefined on the command line, default to debug mode
   SET(CMAKE_BUILD_TYPE "Debug")
   SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel.")
ENDIF()

MESSAGE(STATUS "BUILD TYPE SELECTED: ${CMAKE_BUILD_TYPE}")


