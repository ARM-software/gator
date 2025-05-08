# Copyright (C) 2022 by Arm Limited. All rights reserved.

find_package(Protobuf REQUIRED)

set(Protobuf_IMPORT_DIRS ${CMAKE_CURRENT_LIST_DIR})

protobuf_generate_cpp(PROTO_SRC PROTO_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/shell/perf/capture_configuration.proto
)

add_library(ipcproto STATIC
    ${PROTO_SRC}
    ${PROTO_HEADERS}
)

target_include_directories(ipcproto
    SYSTEM PUBLIC ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(ipcproto
    PRIVATE ${Protobuf_LITE_LIBRARIES}
)

target_compile_options(ipcproto PRIVATE "-Wno-error")

if(ANDROID)
    # The protobuf logging code on Android has references to symbols in the
    # NDK's logging library, we don't use it but the linker has to be appeased
    FIND_LIBRARY(ndk_log_lib log)
    TARGET_LINK_LIBRARIES(ipcproto
        PRIVATE ${ndk_log_lib}
    )
endif()
