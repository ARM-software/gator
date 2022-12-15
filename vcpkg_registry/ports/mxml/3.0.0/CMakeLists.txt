CMAKE_MINIMUM_REQUIRED(VERSION 3.9.0 FATAL_ERROR)

PROJECT(
    mxml
    VERSION 3.0.0
    LANGUAGES C
)

set(HEADERS
    config.h
    mxml.h
    mxml-private.h
)

set(SOURCES
    mxml-attr.c
    mxml-entity.c
    mxml-file.c
    mxml-get.c
    mxml-index.c
    mxml-node.c
    mxml-private.c
    mxml-search.c
    mxml-set.c
    mxml-string.c
)

include(GNUInstallDirs)

add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(
    ${PROJECT_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER ${CMAKE_SOURCE_DIR}/mxml.h)

# Install the library
install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Export
    PUBLIC_HEADER DESTINATION include
)

# Generate and install package cmake file, so it can be found with
# find_package()
install(
    EXPORT ${PROJECT_NAME}Export
    DESTINATION share/${PROJECT_NAME}
    FILE ${PROJECT_NAME}Config.cmake
)

