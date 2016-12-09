# Copyright (C) ARM Limited 2010-2016. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.10 FATAL_ERROR)

PROJECT(gatord)

# Configuration options
OPTION(GATORD_WERROR        "Build with -Werror set"    OFF)

# Include the target detection code
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-target.cmake)

# Include the source MD5 macro
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/create-src-md5.cmake)

# Use ExternalProject to build the native gatord-xml-escape exe
INCLUDE(ExternalProject)

# Include musl libc build commands
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/musl-libc.cmake)

# Native compiler configuration options (for gatord-xml-escape tool)
SET(CMAKE_NATIVE_GENERATOR      "${CMAKE_GENERATOR}"
                                CACHE STRING "Native generator configuration")
SET(CMAKE_NATIVE_C_COMPILER     "gcc"
                                CACHE STRING "Native GCC compiler to use")
SET(CMAKE_NATIVE_CXX_COMPILER   "g++"
                                CACHE STRING "Native GCC C++ compiler to use")

# Configure target flags
SET(GATORD_C_CXX_FLAGS      "-pthread -Wall -fno-exceptions -pthread")
IF(GATORD_WERROR)
    SET(GATORD_C_CXX_FLAGS  "${GATORD_C_CXX_FLAGS} -Werror")
ENDIF()

SET(CMAKE_C_FLAGS           "${CMAKE_C_FLAGS} ${GATORD_C_CXX_FLAGS}")
SET(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} ${GATORD_C_CXX_FLAGS} -fno-rtti -Wextra -Wshadow -Wpointer-arith -Wundef") # -Weffc++ -Wmissing-declarations

IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # add -fno-sized-deallocation
    IF(NOT("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "5.0.0"))
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sized-deallocation")
    ENDIF()
ENDIF()

ADD_DEFINITIONS(-DETCDIR="/etc")
INCLUDE_DIRECTORIES(        ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors
                            ${CMAKE_CURRENT_BINARY_DIR})

# This file is generated and contains a hash of the source files
SET(GENERATED_MD5_SOURCE    ${CMAKE_CURRENT_BINARY_DIR}/SrcMd5.cpp)

# The source files to build
SET(GATORD_SOURCES          ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-private.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-search.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-set.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-entity.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-attr.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-node.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-index.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-string.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-file.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/mxml/mxml-get.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/error.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/data.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/init.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/conf-parse.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/conf-lex.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/general.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/access.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/sysfs.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/Monitor.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/ConfigurationXML.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Fifo.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/MemInfoDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/EventsXML.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/ExternalSource.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/AtraceDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/LocalCapture.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PerfDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Proc.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/FSDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PerfBuffer.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/NetDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/UEvent.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/OlySocket.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PerfSource.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/OlyUtility.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/CapturedXML.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/DiskIODriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Buffer.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/DriverSource.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/ExternalDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/c++.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Child.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Command.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/AnnotateListener.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/SessionData.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Source.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PmuXML.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/CCNDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/UserSpaceSource.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/StreamlineSetup.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PerfGroup.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/HwmonDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Logging.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/MaliVideoDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/MidgardDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/KMod.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/FtraceDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/DynBuf.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Driver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/PolledDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/SimpleDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/DriverCounter.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/SessionXML.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/Sender.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/TtraceDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliDevice.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliInstanceLocator.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrReader.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrDriver.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrSource.cpp
                            ${GENERATED_MD5_SOURCE}
                            ${CMAKE_CURRENT_BINARY_DIR}/defaults_xml.h
                            ${CMAKE_CURRENT_BINARY_DIR}/events_xml.h
                            ${CMAKE_CURRENT_BINARY_DIR}/pmus_xml.h)

# Create a list of files to hash for the generated has file
FILE(GLOB_RECURSE           FILES_TO_HASH
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.h
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.xml
                            ${CMAKE_CURRENT_SOURCE_DIR}/*.cmake
                            ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)

CREATE_SRC_MD5(             "gSrcMd5"
                            ${GENERATED_MD5_SOURCE}
                            ${FILES_TO_HASH})

# Build the merged events.xml
FILE(GLOB                   EVENTS_XML_FILES
                            ${CMAKE_CURRENT_SOURCE_DIR}/events-*.xml)
LIST(SORT                   EVENTS_XML_FILES)
LIST(REMOVE_DUPLICATES      EVENTS_XML_FILES)

# Convert the list to a string that can be passed to the custom command
SET(EVENTS_XML_FILES_STRING "${CMAKE_CURRENT_SOURCE_DIR}/events_header.xml")
FOREACH(EVENTS_XML_FILE     ${EVENTS_XML_FILES})
    SET(EVENTS_XML_FILES_STRING
                            "${EVENTS_XML_FILES_STRING};${EVENTS_XML_FILE}")
ENDFOREACH()
SET(EVENTS_XML_FILES_STRING "${EVENTS_XML_FILES_STRING};${CMAKE_CURRENT_SOURCE_DIR}/events_footer.xml")

SET(CONCATENATED_EVENTS_XML ${CMAKE_CURRENT_BINARY_DIR}/events.xml)
ADD_CUSTOM_COMMAND(OUTPUT   ${CONCATENATED_EVENTS_XML}
                   COMMAND  ${CMAKE_COMMAND} -DOUTPUT_FILE="${CONCATENATED_EVENTS_XML}"
                                             -DFILES_TO_CONCATENATE="${EVENTS_XML_FILES_STRING}"
                                             -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/concatenate-files.cmake
                   DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/events_header.xml
                            ${CMAKE_CURRENT_SOURCE_DIR}/events_footer.xml
                            ${EVENTS_XML_FILES}
                            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/concatenate-files.cmake)

# Build the gatord-xml-escape using the native compiler
SET(GATORD_XML_ESCAPE_TOOL  ${CMAKE_CURRENT_BINARY_DIR}/escape/gatord-xml-escape)

ExternalProject_Add(gatord-xml-escape
    DOWNLOAD_COMMAND        ""
    SOURCE_DIR              "${CMAKE_CURRENT_SOURCE_DIR}/escape"
    CONFIGURE_COMMAND       ${CMAKE_COMMAND} -G "${CMAKE_NATIVE_GENERATOR}" "${CMAKE_CURRENT_SOURCE_DIR}/escape"
                                             -DCMAKE_C_COMPILER=${CMAKE_NATIVE_C_COMPILER}
                                             -DCMAKE_CXX_COMPILER=${CMAKE_NATIVE_CXX_COMPILER}
                                             -DCMAKE_C_FLAGS=
                                             -DCMAKE_CXX_FLAGS=
    INSTALL_COMMAND         ""
    BINARY_DIR              ${CMAKE_CURRENT_BINARY_DIR}/escape
)

ADD_CUSTOM_COMMAND(OUTPUT   ${GATORD_XML_ESCAPE_TOOL}
                   DEPENDS  gatord-xml-escape)

# Macro to aid generation of xml->header files
MACRO(CREATE_XML_HEADER     CONSTANT_NAME
                            SOURCE_XML_FILE
                            OUTPUT_HEADER_FILE)
    ADD_CUSTOM_COMMAND(OUTPUT   ${OUTPUT_HEADER_FILE}
                       COMMAND  ${GATORD_XML_ESCAPE_TOOL} ${CONSTANT_NAME} ${SOURCE_XML_FILE} ${OUTPUT_HEADER_FILE}
                       DEPENDS  gatord-xml-escape
                                ${GATORD_XML_ESCAPE_TOOL}
                                ${SOURCE_XML_FILE})
ENDMACRO()

# Target to generate defaults_xml.h
CREATE_XML_HEADER(          defaults.xml
                            ${CMAKE_CURRENT_SOURCE_DIR}/defaults.xml
                            ${CMAKE_CURRENT_BINARY_DIR}/defaults_xml.h)

# Target to generate pmus_xml.h
CREATE_XML_HEADER(          pmus.xml
                            ${CMAKE_CURRENT_SOURCE_DIR}/pmus.xml
                            ${CMAKE_CURRENT_BINARY_DIR}/pmus_xml.h)

# Target to generate events.h
CREATE_XML_HEADER(          events.xml
                            ${CONCATENATED_EVENTS_XML}
                            ${CMAKE_CURRENT_BINARY_DIR}/events_xml.h)

# Generate the gatord executable
ADD_EXECUTABLE(gatord       ${GATORD_SOURCES})

IF(NOT ANDROID)
    TARGET_LINK_LIBRARIES(gatord    rt
                                    m)
ENDIF()

ADD_MUSL_DEPENDENCY(gatord)


# Installation configuration
IF(NOT DEFINED GATOR_INSTALL_PREFIX)
    SET(GATOR_INSTALL_PREFIX    "share/gator-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

SET(GATORD_INSTALL_DIR      ./${GATOR_INSTALL_PREFIX}/daemon/)

INSTALL(TARGETS             gatord
        RUNTIME DESTINATION ${GATORD_INSTALL_DIR})

INSTALL(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/COPYING
        DESTINATION ${GATORD_INSTALL_DIR})

