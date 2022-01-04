# Copyright (C) 2010-2021 by Arm Limited. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 3.6.3 FATAL_ERROR)

PROJECT(streamline-annotate C)

SET(STREAMLINE_ANNOTATE_SOURCES     ${CMAKE_CURRENT_SOURCE_DIR}/streamline_annotate.c
                                    ${CMAKE_CURRENT_SOURCE_DIR}/streamline_annotate.h
                                    ${CMAKE_CURRENT_SOURCE_DIR}/streamline_annotate_logging.h)

OPTION(TCP_ANNOTATIONS "Use TCP (instead of unix sockets) to send annotations to gator." OFF)
OPTION(CLANG_TIDY_FIX  "Enable --fix with clang-tidy"  OFF)

####
#   Find various optional tools
####
FIND_FILE(CLANG_FORMAT NAMES "clang-format${CMAKE_EXECUTABLE_SUFFIX}"
                       NO_CMAKE_FIND_ROOT_PATH)
FIND_FILE(CLANG_TIDY NAMES "clang-tidy${CMAKE_EXECUTABLE_SUFFIX}"
                     NO_CMAKE_FIND_ROOT_PATH)

####
#   Add a custom targets for running clang-format and clang tidy
####

IF(EXISTS ${CLANG_FORMAT})
    ADD_CUSTOM_TARGET(clang-format
                      COMMAND ${CLANG_FORMAT} -i ${STREAMLINE_ANNOTATE_SOURCES}
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      SOURCES ${STREAMLINE_ANNOTATE_SOURCES}
                      COMMENT "Run clang-format on the sources")
ENDIF()

IF(EXISTS ${CLANG_TIDY})
    ### CMAKE_xxx_CLANG_TIDY need to be before the ADD_LIBRARY
    OPTION(ENABLE_CLANG_TIDY_DURING_BUILD   "Compile and tidy at the same time" ON)
    IF(ENABLE_CLANG_TIDY_DURING_BUILD)
        SET(CMAKE_C_CLANG_TIDY      ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR})
    ENDIF()
    ADD_CUSTOM_TARGET(clang-tidy
                      COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} $<$<BOOL:${CLANG_TIDY_FIX}>:--fix> ${STREAMLINE_ANNOTATE_SOURCES}
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      SOURCES ${STREAMLINE_ANNOTATE_SOURCES}
                      COMMENT "Run clang-tidy on the sources")
ENDIF()

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

####
# Create the library
####

ADD_LIBRARY(streamline_annotate-static  STATIC
            ${STREAMLINE_ANNOTATE_SOURCES})

ADD_LIBRARY(streamline_annotate-shared  SHARED
            ${STREAMLINE_ANNOTATE_SOURCES})

# Set the name to "libstreamline_annotate.a"
SET_TARGET_PROPERTIES(streamline_annotate-static
                      PROPERTIES    OUTPUT_NAME     "streamline_annotate")

# Set the name to "libstreamline_annotate.so"
SET_TARGET_PROPERTIES(streamline_annotate-shared
                      PROPERTIES    OUTPUT_NAME     "streamline_annotate")

# Installation configuration
IF(NOT DEFINED GATOR_INSTALL_PREFIX)
    SET(GATOR_INSTALL_PREFIX        "share/gator-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

IF(TCP_ANNOTATIONS)
    add_definitions(-DTCP_ANNOTATIONS)
ENDIF()

SET(STREAMLINE_ANNOTATE_INSTALL_DIR ./${GATOR_INSTALL_PREFIX}/annotations/)

INSTALL(TARGETS     streamline_annotate-static
                    streamline_annotate-shared
        ARCHIVE DESTINATION         ${STREAMLINE_ANNOTATE_INSTALL_DIR}
        LIBRARY DESTINATION         ${STREAMLINE_ANNOTATE_INSTALL_DIR})

INSTALL(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/readme.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
                    ${STREAMLINE_ANNOTATE_SOURCES}
        DESTINATION ${STREAMLINE_ANNOTATE_INSTALL_DIR})
