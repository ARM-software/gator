# Copyright (C) 2022-2023 by Arm Limited (or its affiliates). All rights reserved.

# ###
# Find various optional tools
# ###
find_file(CLANG_FORMAT NAMES "clang-format${CMAKE_EXECUTABLE_SUFFIX}"
    NO_CMAKE_FIND_ROOT_PATH)
find_file(CLANG_TIDY NAMES "clang-tidy${CMAKE_EXECUTABLE_SUFFIX}"
    NO_CMAKE_FIND_ROOT_PATH)
find_file(CLANG_TIDY_REPLACEMENTS NAMES "clang-apply-replacements${CMAKE_EXECUTABLE_SUFFIX}"
    NO_CMAKE_FIND_ROOT_PATH)

# ###
# Add a custom targets for running clang-format and clang tidy
# ###
if(EXISTS ${CLANG_FORMAT})
    add_custom_target(clang-format
        COMMENT "Run clang-format on all targets")
endif()

if((EXISTS ${CLANG_TIDY}) AND(EXISTS ${CLANG_TIDY_REPLACEMENTS}))
    if(NOT EXISTS ${CLANG_TIDY_YAML})
        message(FATAL_ERROR "Could not find ${CLANG_TIDY_YAML}")
    endif()

    # ## CMAKE_xxx_CLANG_TIDY need to be before the ADD_EXECUTABLE
    OPTION(ENABLE_CLANG_TIDY_DURING_BUILD "Compile and tidy at the same time" OFF)

    add_custom_target(clang-tidy
        COMMENT "Run clang-tidy on all targets")
    add_custom_target(clang-tidy-fix
        COMMENT "Run clang-tidy --fix on all targets")
    add_custom_target(clang-tidy-apply
        COMMAND ${CLANG_TIDY_REPLACEMENTS} --format --style=file --ignore-insert-conflict --remove-change-desc-files "${CMAKE_BINARY_DIR}/clang-tidy-fix/"
        DEPENDS clang-tidy-fix
        COMMENT "Run clang-apply-replacements")
endif()

function(add_clang_tools TARGET_NAME)
    IF(ENABLE_CLANG_TIDY_DURING_BUILD)
        set_target_properties(${TARGET_NAME} PROPERTIES
            C_CLANG_TIDY "${CLANG_TIDY};-p;${CMAKE_BINARY_DIR}"
            CXX_CLANG_TIDY "${CLANG_TIDY};-p;${CMAKE_BINARY_DIR}"
        )
    endif()

    if(EXISTS ${CLANG_FORMAT})
        add_custom_target(clang-format-${TARGET_NAME}
            COMMAND ${CLANG_FORMAT} -i ${ARGN}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            SOURCES ${ARGN}
            COMMENT "Run clang-format on the sources in ${CMAKE_CURRENT_SOURCE_DIR} for ${TARGET_NAME}")
        add_dependencies(clang-format clang-format-${TARGET_NAME})
    endif()

    if((EXISTS ${CLANG_TIDY}) AND(EXISTS ${CLANG_TIDY_REPLACEMENTS}))
        set(outputs)
        set(outputs_fix)

        foreach(file ${ARGN})
            file(RELATIVE_PATH file "${CMAKE_CURRENT_SOURCE_DIR}" "${file}")

            if("${file}" MATCHES "\.(cpp|cc|c)$")
                # run clang tidy
                set(item "${CMAKE_BINARY_DIR}/clang-tidy-${TARGET_NAME}/${file}")
                add_custom_command(OUTPUT ${item}
                    COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} ${file}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    DEPENDS ${CLANG_TIDY} ${CLANG_TIDY_YAML} ${file}
                    COMMENT "Running clang-tidy on ${item}")
                set(outputs ${outputs} ${item})
                set_source_files_properties(${item} PROPERTIES SYMBOLIC ON)

                # run clang tidy --fix
                string(SHA256 item "${file}")
                set(item "${CMAKE_BINARY_DIR}/clang-tidy-fix/${TARGET_NAME}/${item}.yaml")
                add_custom_command(OUTPUT ${item}
                    COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --export-fixes="${item}" ${file}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    DEPENDS ${CLANG_TIDY} ${CLANG_TIDY_YAML} ${file}
                    IMPLICIT_DEPENDS CXX ${file}
                    COMMENT "Running clang-tidy on ${item}")
                set(outputs_fix ${outputs_fix} ${item})
            endif()
        endforeach(file ${ARGN})

        add_custom_target(clang-tidy-${TARGET_NAME}
            DEPENDS ${outputs}
            SOURCES ${ARGN}
            COMMENT "Run clang-tidy on the sources in ${CMAKE_CURRENT_SOURCE_DIR} for ${TARGET_NAME}")
        add_custom_target(clang-tidy-fix-${TARGET_NAME}
            DEPENDS ${outputs_fix}
            SOURCES ${ARGN}
            COMMENT "Run clang-tidy on the sources in ${CMAKE_CURRENT_SOURCE_DIR} for ${TARGET_NAME}")
        add_dependencies(clang-tidy clang-tidy-${TARGET_NAME})
        add_dependencies(clang-tidy-fix clang-tidy-fix-${TARGET_NAME})
    endif()
endfunction()
