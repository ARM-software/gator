# Copyright (C) 2010-2022 by Arm Limited. All rights reserved.

# Save this outside the macro so that development build will retrigger the generation of the source file if this file changes
SET(CREATE_SRC_MD5_CMAKE_FILE ${CMAKE_CURRENT_LIST_FILE})

#
# Macro to create a source file containing the md5 hash of the list of provided files
#
MACRO(CREATE_SRC_MD5 CONSTANT_NAME ID_NAME ID_VALUE OUTPUT_FILE HASH_FILE)
    # Files to hash are the arguments after the expected arguments
    SET(FILES_TO_HASH ${ARGN})
    LIST(SORT FILES_TO_HASH)
    LIST(REMOVE_DUPLICATES FILES_TO_HASH)

    # Convert the list to a string that can be passed to the custom command
    SET(FILES_TO_HASH_STRING)

    FOREACH(FILE_TO_HASH ${FILES_TO_HASH})
        FILE(RELATIVE_PATH FILE_TO_HASH
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${FILE_TO_HASH}")
        SET(FILES_TO_HASH_STRING "${FILES_TO_HASH_STRING};${FILE_TO_HASH}")
    ENDFOREACH()

    # Target to generate OUTPUT_FILE destination file
    SET(CREATE_SRC_MD5_RUNNER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/create-src-md5-runner.cmake")
    ADD_CUSTOM_COMMAND(OUTPUT ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -DCONSTANT_NAME="${CONSTANT_NAME}"
        -DOUTPUT_FILE="${OUTPUT_FILE}"
        -DHASH_FILE="${HASH_FILE}"
        -DFILES_TO_HASH="${FILES_TO_HASH_STRING}"
        -DID_NAME="${ID_NAME}"
        -DID_VALUE="${ID_VALUE}"
        -P ${CREATE_SRC_MD5_RUNNER_FILE}
        DEPENDS ${FILES_TO_HASH}
        ${CREATE_SRC_MD5_CMAKE_FILE}
        ${CREATE_SRC_MD5_RUNNER_FILE}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
ENDMACRO()
