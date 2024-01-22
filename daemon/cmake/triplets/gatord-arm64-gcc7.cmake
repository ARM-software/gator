set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(NOT CMAKE_HOST_SYSTEM_PROCESSOR)
    execute_process(COMMAND "uname" "-m" OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_PROCESSOR OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../../cmake/aarch64-gcc7.toolchain.cmake)

# C++17 should be enabled for all targets
set(_VCPKG_CMAKE_CXX_FLAGS  "-std=gnu++1z -fexceptions -frtti -fvisibility=hidden")

# Configure the default compiler options
set(VCPKG_CMAKE_CONFIGURE_OPTIONS   "-DCMAKE_CXX_FLAGS:STRING=${_VCPKG_CMAKE_CXX_FLAGS}"
                                    "-DCMAKE_CXX_STANDARD:STRING=17"
                                    "-DCMAKE_CXX_STANDARD_REQUIRED:BOOL=ON")

