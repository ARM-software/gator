# Save this outside the macro so that development build will retrigger the generation of the source file if this file changes
SET(ESCAPE_CMAKE_FILE               "${CMAKE_CURRENT_LIST_FILE}")
SET(ESCAPE_TEMPLATE                 "${CMAKE_CURRENT_LIST_DIR}/escape.template")

#
#   Macro to create a source file containing a C string encoded with the contents of the input file
#
FUNCTION(ESCAPE_FILE_TO_C_STRING    CONSTANT_NAME
                                    INPUT_FILE
                                    OUTPUT_FILE)
    # Target to generate OUTPUT_FILE destination file
    SET(ESCAPE_RUNNER_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/escape-runner.cmake")
    ADD_CUSTOM_COMMAND(OUTPUT       "${OUTPUT_FILE}"
                       COMMAND      "${CMAKE_COMMAND}"  -DCONSTANT_NAME="${CONSTANT_NAME}"
                                                        -DINPUT_FILE="${INPUT_FILE}"
                                                        -DOUTPUT_FILE="${OUTPUT_FILE}"
                                                        -DESCAPE_TEMPLATE="${ESCAPE_TEMPLATE}"
                                                        -P "${ESCAPE_RUNNER_FILE}"
                       DEPENDS      "${INPUT_FILE}"
                                    "${ESCAPE_CMAKE_FILE}"
                                    "${ESCAPE_RUNNER_FILE}"
                                    "${ESCAPE_TEMPLATE}"
                       WORKING_DIRECTORY             "${CMAKE_CURRENT_SOURCE_DIR}")
ENDFUNCTION()
