#
#   The name of the zipped up split-debug file
#
IF(GATORD_SPLIT_DEBUG_ZIP)
    MESSAGE(STATUS "Using provided GATORD_SPLIT_DEBUG_ZIP = ${GATORD_SPLIT_DEBUG_ZIP}")
ELSE()
    SET(GATORD_SPLIT_DEBUG_ZIP  "split-debug.zip")
    MESSAGE(STATUS "Using default GATORD_SPLIT_DEBUG_ZIP = ${GATORD_SPLIT_DEBUG_ZIP}")
ENDIF()

#
#   Macro for stripping/splitting out the debug
#
GET_FILENAME_COMPONENT(SPLIT_DEBUG_LOCATION "${CMAKE_BINARY_DIR}/split-debug/" ABSOLUTE)
MACRO(STRIP_TARGET TARGET_NAME)
    IF (${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
        FILE(MAKE_DIRECTORY "${SPLIT_DEBUG_LOCATION}")
        SET(TARGET_NAME_DEBUG_FILE  "${SPLIT_DEBUG_LOCATION}/$<TARGET_FILE_NAME:${TARGET_NAME}>.debug")
        ADD_CUSTOM_COMMAND( TARGET ${TARGET_NAME} POST_BUILD
                            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${TARGET_NAME}>" "${TARGET_NAME_DEBUG_FILE}"
                            COMMAND ${CMAKE_STRIP} --strip-all "$<TARGET_FILE:${TARGET_NAME}>"
                            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink="${TARGET_NAME_DEBUG_FILE}" "$<TARGET_FILE:${TARGET_NAME}>"
                            COMMENT "Stripping ${TARGET_NAME}, generating $<TARGET_FILE:${TARGET_NAME}>.debug")
        ADD_DEPENDENCIES(create-split-debug-zip "${TARGET_NAME}")
        LIST(APPEND ADDITIONAL_CLEAN_FILES "${TARGET_NAME_DEBUG_FILE}")
    ELSEIF (NOT(${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
        ADD_CUSTOM_COMMAND( TARGET ${TARGET_NAME} POST_BUILD
                            COMMAND ${CMAKE_STRIP} --strip-all "$<TARGET_FILE:${TARGET_NAME}>"
                            COMMENT "Stripping ${TARGET_NAME}")
    ENDIF()
ENDMACRO()

#
#   Build the split-debug zip file
#
IF (${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    ADD_CUSTOM_COMMAND(OUTPUT "${CMAKE_BINARY_DIR}/${GATORD_SPLIT_DEBUG_ZIP}"
                       COMMAND zip -9rvj "${CMAKE_BINARY_DIR}/${GATORD_SPLIT_DEBUG_ZIP}" "${SPLIT_DEBUG_LOCATION}"
                       DEPENDS "${SPLIT_DEBUG_LOCATION}")
    ADD_CUSTOM_TARGET(create-split-debug-zip ALL
                      DEPENDS "${CMAKE_BINARY_DIR}/${GATORD_SPLIT_DEBUG_ZIP}" "${SPLIT_DEBUG_LOCATION}"
                      COMMENT "Zipping up split debug files")
    LIST(APPEND ADDITIONAL_CLEAN_FILES "${CMAKE_BINARY_DIR}/${GATORD_SPLIT_DEBUG_ZIP}")
ENDIF()

