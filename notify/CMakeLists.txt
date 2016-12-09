# Copyright (C) ARM Limited 2010-2016. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(notify-dex)

SET(NOTIFY_SOURCE_DIR                   ${CMAKE_CURRENT_SOURCE_DIR})
SET(NOTIFY_SOURCE_FILES                 ${NOTIFY_SOURCE_DIR}/Notify.java)
SET(NOTIFY_CLASS_OUTDIR                 ${CMAKE_CURRENT_BINARY_DIR})
SET(NOTIFY_DEX_PATH                     ${CMAKE_CURRENT_BINARY_DIR}/notify.dex)

# Parse the relative paths to the source files to get the class paths
SET(NOTIFY_CLASS_FILES                  )
SET(NOTIFY_CLASS_PATHS                  )
FOREACH(NOTIFY_SOURCE_FILE ${NOTIFY_SOURCE_FILES})
    GET_FILENAME_COMPONENT(             NOTIFY_SOURCE_FILE_DIRECTORY
                                        ${NOTIFY_SOURCE_FILE}
                                        PATH)
    GET_FILENAME_COMPONENT(             NOTIFY_SOURCE_FILE_NAME_WE
                                        ${NOTIFY_SOURCE_FILE}
                                        NAME_WE)

    SET(NOTIFY_SOURCE_FILE              "${NOTIFY_SOURCE_FILE_DIRECTORY}/${NOTIFY_SOURCE_FILE_NAME_WE}.class")

    FILE(RELATIVE_PATH                  NOTIFY_CLASS_FILE
                                        ${NOTIFY_SOURCE_DIR}
                                        ${NOTIFY_SOURCE_FILE})

    SET(NOTIFY_CLASS_FILES              ${NOTIFY_CLASS_FILES} "${NOTIFY_CLASS_FILE}")
    SET(NOTIFY_CLASS_PATHS              ${NOTIFY_CLASS_PATHS} "${NOTIFY_CLASS_OUTDIR}/${NOTIFY_CLASS_FILE}")
ENDFOREACH()

# Locate the android SDK base path
IF((NOT DEFINED ANDROID_HOME) OR ("${ANDROID_HOME}" STREQUAL ""))
    IF("$ENV{ANDROID_HOME}" STREQUAL "")
        MESSAGE(FATAL_ERROR             "ANDROID_HOME is not set, and neither is the environment variable")
    ENDIF()
    SET(ANDROID_HOME                    "$ENV{ANDROID_HOME}")
ENDIF()

MESSAGE(STATUS                          "ANDROID_HOME detected as '${ANDROID_HOME}'")

IF(NOT IS_DIRECTORY ${ANDROID_HOME})
    MESSAGE(FATAL_ERROR                 "ANDROID_HOME does not point to a valid path")
ENDIF()

# Find the oldest SDK Platform installed >= 16
FILE(GLOB                               SDK_PLATFORMS
                                        ${ANDROID_HOME}/platforms/android-1[6-9]/
                                        ${ANDROID_HOME}/platforms/android-[2-9][0-9]/)
LIST(SORT                               SDK_PLATFORMS)
LIST(REMOVE_DUPLICATES                  SDK_PLATFORMS)
LIST(LENGTH                             SDK_PLATFORMS
                                        SDK_PLATFORMS_LENGTH)

IF(${SDK_PLATFORMS_LENGTH} EQUAL 0)
    MESSAGE(FATAL_ERROR                 "No SDK platforms found in ${ANDROID_HOME}/platforms/ for Android target levels >= 16")
ENDIF()

LIST(GET                                SDK_PLATFORMS
                                        0
                                        SDK_PLATFORM)

MESSAGE(STATUS                          "Using Android SDK platform '${SDK_PLATFORM}'")

# Find location of layoutlib.jar
FIND_FILE(ANDROID_LAYOUTLIB_PATH        "layoutlib.jar"
          PATHS                         "${SDK_PLATFORM}"
                                        "${SDK_PLATFORM}/*/"
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
          DOC                           "Path to Android platform SDK layoutlib.jar")

IF(NOT EXISTS "${ANDROID_LAYOUTLIB_PATH}")
    MESSAGE(FATAL_ERROR                 "Could not find layoutlib.jar in ${SDK_PLATFORM}")
ENDIF()

MESSAGE(STATUS                          "Android layoutlib.jar detected as '${ANDROID_LAYOUTLIB_PATH}'")

# Find the location of DX command which is either on the PATH or in the newest SDK build tools dir
FILE(GLOB                               SDK_BUILDTOOLS
                                        ${ANDROID_HOME}/build-tools/*/)
LIST(SORT                               SDK_BUILDTOOLS)
LIST(REMOVE_DUPLICATES                  SDK_BUILDTOOLS)
LIST(REVERSE                            SDK_BUILDTOOLS)
LIST(LENGTH                             SDK_BUILDTOOLS
                                        SDK_BUILDTOOLS_LENGTH)
LIST(GET                                SDK_BUILDTOOLS
                                        0
                                        SDK_BUILDTOOL)

FIND_FILE(DX_COMMAND
          NAMES                         "dx"
                                        "dx.bat"
          PATHS                         ENV PATH
                                        ${SDK_BUILDTOOL}
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
          DOC                           "Path to Android build tools 'dx' command")

IF(NOT EXISTS "${DX_COMMAND}")
    MESSAGE(FATAL_ERROR                 "Could not find 'dx' command")
ENDIF()

MESSAGE(STATUS                          "Android 'dx' command detected as '${DX_COMMAND}'")

# Find the location of the javac command on the PATH
FIND_FILE(JAVAC
          NAMES                         "javac"
                                        "javac.exe"
          PATHS                         ENV PATH
          NO_CMAKE_FIND_ROOT_PATH
          DOC                           "Path to 'javac' command")

IF(NOT EXISTS "${JAVAC}")
    MESSAGE(FATAL_ERROR                 "Could not find 'javac' command")
ENDIF()

MESSAGE(STATUS                          "'javac' command detected as '${JAVAC}'")

# Compile the java -> class file
ADD_CUSTOM_COMMAND(OUTPUT               ${NOTIFY_CLASS_PATHS}
                   COMMAND              ${JAVAC} -cp ${ANDROID_LAYOUTLIB_PATH} -source 1.6 -target 1.6 ${NOTIFY_SOURCE_FILES} -d ${NOTIFY_CLASS_OUTDIR}
                   WORKING_DIRECTORY    ${NOTIFY_CLASS_OUTDIR}
                   DEPENDS              ${NOTIFY_SOURCE_FILES}
                   COMMENT              "Compiling Notify.java")

# Create the dex file from the class file
ADD_CUSTOM_COMMAND(OUTPUT               ${NOTIFY_DEX_PATH}
                   COMMAND              ${DX_COMMAND} --dex --output=${NOTIFY_DEX_PATH} ${NOTIFY_CLASS_FILES}
                   WORKING_DIRECTORY    ${NOTIFY_CLASS_OUTDIR}
                   DEPENDS              ${NOTIFY_CLASS_PATHS}
                   COMMENT              "Creating notify.dex")

# The custom target ensure that everything gets build as part of the 'all' target
ADD_CUSTOM_TARGET(notify-dex            ALL
                  DEPENDS               ${NOTIFY_DEX_PATH})

# Installation configuration
IF(NOT DEFINED GATOR_INSTALL_PREFIX)
    SET(GATOR_INSTALL_PREFIX            "share/gator-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

SET(NOTIFY_INSTALL_DIR                  ./${GATOR_INSTALL_PREFIX}/notify/)

INSTALL(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/COPYING
                    ${NOTIFY_DEX_PATH}
                    ${NOTIFY_SOURCE_FILES}
        DESTINATION ${NOTIFY_INSTALL_DIR})

