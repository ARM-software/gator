#!/bin/bash

source build-common.sh

show_help() {
    echo "Usage:"
    echo "    $0 [-n <ndk>] [-a <api>] [-t <target>] [-g <generator>] [-o <path>] [-c <cmake>] [-l <mode>] [-d] [-s]"
    echo "Where:"
    echo "    -p <profile>      - Specify the predefined profile to build. Defaults to "
    echo "                        'native-gcc' if not specified. Must be one of "
    echo "                        'native-gcc', 'native-clang', 'arm-glibc', 'arm-musl', "
    echo "                        'arm64-glibc' or 'arm64-musl'."
    echo "    -g <generator>    - Specify the CMake generator to use. Defaults to Ninja if"
    echo "                        it is available, otherwise to whatever the CMake default"
    echo "                        is."
    echo "    -o <path>         - Specify the build directory. Defaults to "
    echo "                        build-<profile>-<release|debug>"
    echo "    -c <cmake>        - CMake binary path. Defaults to 'cmake' if not specified."
    echo "    -l <mode>         - Configure LTO. Default is 'default' which turns on LTO "
    echo "                        and uses ld.lld if profile is native-clang and ld.lld is"
    echo "                        available, otherwise uses ld.gold. Other options are "
    echo "                        'off' to disable LTO, 'gold' to force the use of ld.gold,"
    echo "                        and 'lld' to force the use of ld.lld."
    echo "    -d                - Build a debug version of gatord instead of a "
    echo "                        release version."
    echo "    -s                - Tell vcpkg to use the provided binaries instead"
    echo "                        providing its own."
    echo "    -v                - Enable verbose output."
    exit 0
}

# setup some defaults
target=native-gcc
lto=default

# parse arguments
while getopts ":hp:g:c:o:l:sdv" arg; do
    case $arg in
        h)
            show_help
            ;;
        p)
            target="${OPTARG}"
            ;;
        g)
            cmake_generator="${OPTARG}"
            ;;
        o)
            build_path="${OPTARG}"
            ;;
        c)
            cmake_exe="${OPTARG}"
            ;;
        l)
            lto="${OPTARG}"
            ;;
        d)
            build_type=Debug
            path_suffix=dbg
            ;;
        s)
            use_system_binaries=y
            ;;
        v)
            verbose=y
            ;;
        *)
            show_help
            ;;
    esac
done

# validate the arguments
if [ "${target}" = "native-gcc" ]; then
    build_args=( -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/native-gcc.toolchain.cmake" )
elif [ "${target}" = "native-clang" ]; then
    build_args=( -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/native-clang.toolchain.cmake" )
elif [ "${target}" = "arm-glibc" ]; then
    build_args=( -DVCPKG_TARGET_TRIPLET=gatord-armv7-linux -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/arm-linux-hardfloat.toolchain.cmake" )
elif [ "${target}" = "arm-musl" ]; then
    build_args=( -DVCPKG_TARGET_TRIPLET=gatord-armv7-linux-musl -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/arm-linux-musleabihf.toolchain.cmake" -DGATORD_BUILD_STATIC:BOOLEAN=ON )
elif [ "${target}" = "arm64-glibc" ]; then
    build_args=( -DVCPKG_TARGET_TRIPLET=gatord-arm64-linux -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/aarch64-linux.toolchain.cmake" )
elif [ "${target}" = "arm64-musl" ]; then
    build_args=( -DVCPKG_TARGET_TRIPLET=gatord-arm64-linux-musl -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/aarch64-linux-musleabi.toolchain.cmake" -DGATORD_BUILD_STATIC:BOOLEAN=ON )
else
    echo "Error: Unrecognized value for target: '${target}'"
    exit 1
fi

if [ "${lto}" = "default" ]; then
    if [ "${target}" = "native-clang" ] && which ld.lld; then
        build_args+=( -DCMAKE_ENABLE_LTO:BOOL=ON -DCMAKE_USE_LLD:BOOL=ON )
    else
        build_args+=( -DCMAKE_ENABLE_LTO:BOOL=ON -DCMAKE_USE_LLD:BOOL=OFF )
    fi
elif [ "${lto}" = "off" ]; then
    build_args+=( -DCMAKE_ENABLE_LTO:BOOL=OFF -DCMAKE_USE_LLD:BOOL=OFF )
elif [ "${lto}" = "gold" ]; then
    build_args+=( -DCMAKE_ENABLE_LTO:BOOL=ON -DCMAKE_USE_LLD:BOOL=OFF )
elif [ "${lto}" = "lld" ]; then
    build_args+=( -DCMAKE_ENABLE_LTO:BOOL=ON -DCMAKE_USE_LLD:BOOL=ON )
else
    echo "Error: Invalid value for lto: '${lto}'"
    exit 1
fi

# Bootstrap vcpkg
checkout_vcpkg "${gator_dir}" "${use_system_binaries}"
# and perfetto
checkout_perfetto "${gator_dir}"

# check cmake exe
cmake_exe=$(validate_and_return_cmake_exe "${cmake_exe}")
if [ -z "${cmake_exe}" ]; then
    echo "Error: CMake executable could not be found or was invalid"
    exit 1
fi

# configure generator
cmake_generator=$(cmake_generator_or_default "${cmake_generator}")

# run the build...
if [ -z "${build_path}" ]; then
    build_path="${gator_dir}/build-${target}-${path_suffix}"
fi

build_args+=( -DCMAKE_BUILD_TYPE="${build_type}" )

echo "Running cmake build for target=${target}:"
run_cmake "${cmake_exe}" "${cmake_generator}" "${src_path}" "${build_path}" "${use_system_binaries}" "${verbose}" "${build_args[@]}"

