# Copyright (C) ARM Limited 2010-2016. All rights reserved.

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(setup)

SET(CROSS_COMPILE_ARM                   "arm-linux-gnueabihf-"
                                        CACHE STRING "ARM Cross compiler prefix")

SET(CROSS_COMPILE_AARCH64               "aarch64-linux-gnu-"
                                        CACHE STRING "AARCH64 Cross compiler prefix")

# Macro to compile for each target
MACRO(COMPILE_ELF ASM_FILE CMD_PREFIX AS_ARGS LD_TEXT_BASE)
    # Determine the generated output paths
    GET_FILENAME_COMPONENT(             ASM_NAME_WE
                                        ${ASM_FILE}
                                        NAME_WE)

    SET(TARGET                          "${ASM_NAME_WE}-all")
    SET(OUT_O_PATH                      "${CMAKE_CURRENT_BINARY_DIR}/${ASM_NAME_WE}.o")
    SET(OUT_ELF_PATH                    "${CMAKE_CURRENT_BINARY_DIR}/${ASM_NAME_WE}.elf")
    SET(OUT_BIN_PATH                    "${CMAKE_CURRENT_BINARY_DIR}/${ASM_NAME_WE}")

    SET(AS_CMD_NAME                     "${ASM_NAME_WE}_AS_CMD")
    SET(LD_CMD_NAME                     "${ASM_NAME_WE}_LD_CMD")
    SET(OBJCOPY_CMD_NAME                "${ASM_NAME_WE}_OBJCOPY_CMD")

    # Find the location of the 'as' command on the PATH
    IF(EXISTS "${CMD_PREFIX}as")
        SET(${AS_CMD_NAME}              "${CMD_PREFIX}as")
    ELSE()
        FIND_FILE(${AS_CMD_NAME}
                  NAMES                 "${CMD_PREFIX}as"
                                        "${CMD_PREFIX}as.exe"
                  PATHS                 ENV PATH
                  NO_CMAKE_FIND_ROOT_PATH
                  DOC                   "Path to 'as' command for ${CMD_PREFIX}")
    ENDIF()

    SET(AS_CMD                          ${${AS_CMD_NAME}})

    IF(NOT EXISTS "${AS_CMD}")
        MESSAGE(FATAL_ERROR             "Could not find 'as' command for ${CMD_PREFIX}")
    ENDIF()

    MESSAGE(STATUS                      "'as' command detected as '${AS_CMD}' for ${CMD_PREFIX}")

    # Find the location of the 'ld' command on the PATH
    IF(EXISTS "${CMD_PREFIX}ld")
        SET(${LD_CMD_NAME}              "${CMD_PREFIX}ld")
    ELSE()
        FIND_FILE(${LD_CMD_NAME}
                  NAMES                 "${CMD_PREFIX}ld"
                                        "${CMD_PREFIX}ld.exe"
                  PATHS                 ENV PATH
                  NO_CMAKE_FIND_ROOT_PATH
                  DOC                   "Path to 'ld' command for ${CMD_PREFIX}")
    ENDIF()

    SET(LD_CMD                          ${${LD_CMD_NAME}})

    IF(NOT EXISTS "${LD_CMD}")
        MESSAGE(FATAL_ERROR             "Could not find 'ld' command for ${CMD_PREFIX}")
    ENDIF()

    MESSAGE(STATUS                      "'ld' command detected as '${LD_CMD}' for ${CMD_PREFIX}")

    # Find the location of the 'objcopy' command on the PATH
    IF(EXISTS "${CMD_PREFIX}objcopy")
        SET(${OBJCOPY_CMD_NAME}         "${CMD_PREFIX}objcopy")
    ELSE()
        FIND_FILE(${OBJCOPY_CMD_NAME}
                  NAMES                 "${CMD_PREFIX}objcopy"
                                        "${CMD_PREFIX}objcopy.exe"
                  PATHS                 ENV PATH
                  NO_CMAKE_FIND_ROOT_PATH
                  DOC                   "Path to 'objcopy' command for ${CMD_PREFIX}")
    ENDIF()

    SET(OBJCOPY_CMD                     ${${OBJCOPY_CMD_NAME}})

    IF(NOT EXISTS "${OBJCOPY_CMD}")
        MESSAGE(FATAL_ERROR             "Could not find 'objcopy' command for ${CMD_PREFIX}")
    ENDIF()

    MESSAGE(STATUS                      "'objcopy' command detected as '${OBJCOPY_CMD}' for ${CMD_PREFIX}")


    # Assemble the input file
    ADD_CUSTOM_COMMAND(OUTPUT           ${OUT_O_PATH}
                       COMMAND          ${AS_CMD} ${AS_ARGS} -o ${OUT_O_PATH} ${ASM_FILE}
                       DEPENDS          ${ASM_FILE}
                       COMMENT          "Assembling ${ASM_NAME_WE}.s -> ${ASM_NAME_WE}.o")

    # Link the .o file
    ADD_CUSTOM_COMMAND(OUTPUT           ${OUT_ELF_PATH}
                       COMMAND          ${LD_CMD} -Ttext ${LD_TEXT_BASE} -o ${OUT_ELF_PATH} ${OUT_O_PATH}
                       DEPENDS          ${OUT_O_PATH}
                       COMMENT          "Linking ${ASM_NAME_WE}.o -> ${ASM_NAME_WE}.elf")

    # Extract the elf file using objcopy
    ADD_CUSTOM_COMMAND(OUTPUT           ${OUT_BIN_PATH}
                       COMMAND          ${OBJCOPY_CMD} -O binary ${OUT_ELF_PATH} ${OUT_BIN_PATH}
                       DEPENDS          ${OUT_ELF_PATH}
                       COMMENT          "Linking ${ASM_NAME_WE}.elf -> ${ASM_NAME_WE}")

    # Custom target ensures is part of 'make all'
    ADD_CUSTOM_TARGET(${TARGET}         ALL
                      DEPENDS           ${OUT_BIN_PATH})

ENDMACRO()


COMPILE_ELF("${CMAKE_CURRENT_SOURCE_DIR}/armv7.s"
            "${CROSS_COMPILE_ARM}"
            "-mfpu=vfp"
            10000)

COMPILE_ELF("${CMAKE_CURRENT_SOURCE_DIR}/aarch64.s"
            "${CROSS_COMPILE_AARCH64}"
            ""
            400000)

# Installation configuration
IF(NOT DEFINED GATOR_INSTALL_PREFIX)
    SET(GATOR_INSTALL_PREFIX            "share/gator-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

SET(SETUP_INSTALL_DIR      ./${GATOR_INSTALL_PREFIX}/setup/)

INSTALL(FILES       ${CMAKE_CURRENT_SOURCE_DIR}/gator_setup
        DESTINATION ${SETUP_INSTALL_DIR})

INSTALL(FILES       ${CMAKE_CURRENT_BINARY_DIR}/armv7
                    ${CMAKE_CURRENT_BINARY_DIR}/aarch64
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${SETUP_INSTALL_DIR})

