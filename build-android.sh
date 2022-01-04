#!/bin/bash

source build-common.sh

show_help() {
    echo "Usage:"
    echo "    $0 [-n <ndk>] [-a <api>] [-t <target>] [-g <generator>] [-o <path>] [-c <cmake>] [-d] [-s]"
    echo "Where:"
    echo "    -n <ndk>          - Specify the path to the Android NDK."
    echo "                        Will take the value from the ANDROID_NDK_HOME "
    echo "                        environment variable by default."
    echo "    -a <api>          - Specify the target API level to compile against."
    echo "                        The default is 21, meaning the binary is "
    echo "                        compatible with Android L and later."
    echo "    -t <target>       - Specify the target architecture. Must be one of"
    echo "                        arm64, arm, or x86_64. The default is arm64."
    echo "    -g <generator>    - Specify the CMake generator to use. Defaults to Ninja if"
    echo "                        it is available, otherwise to whatever the CMake default"
    echo "                        is."
    echo "    -o <path>         - Specify the build directory. Defaults to "
    echo "                        build-<target>-<api>-<release|debug>"
    echo "    -c <cmake>        - CMake binary path. Defaults to 'cmake' if not specified."
    echo "    -l <mode>         - Configure LTO. Default is 'default' which turns on LTO."
    echo "                        Other option is 'off' to disable LTO."
    echo "    -d                - Build a debug version of gatord instead of a "
    echo "                        release version."
    echo "    -s                - Tell vcpkg to use the provided binaries instead"
    echo "                        providing its own."
    echo "    -v                - Enable verbose output."
    exit 0
}

# setup some defaults
api_level=21
lto=default
ndk_path="${ANDROID_NDK_HOME}"
target=arm64

# parse arguments
while getopts ":hn:a:t:g:c:o:l:sdv" arg; do
    case $arg in
        h)
            show_help
            ;;
        n)
            ndk_path="${OPTARG}"
            ;;
        a)
            api_level="${OPTARG}"
            ;;
        t)
            target="${OPTARG}"
            ;;
        g)
            cmake_generator="${OPTARG}"
            ;;
        o)
            build_path="${OPTARGS}"
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
if [ -z "${ndk_path}" ]; then
    echo "Error: No NDK path was specified, and ANDROID_NDK_HOME environment variable is"
    echo "not set."
    exit 1
fi
if [ ! -d "${ndk_path}" ]; then
    echo "Error: ${ndk_path} does not exist, or is not a directory"
    exit 1
else
    ndk_path=`realpath "${ndk_path}"`
fi
if [ "${target}" = "arm64" ]; then
    abi="arm64-v8a"
    toolchain="aarch64-linux-android-clang"
    triplet="arm64-android"
elif [ "${target}" = "arm" ]; then
    abi="armeabi-v7a"
    toolchain="arm-linux-androideabi-clang"
    triplet="arm-android"
elif [ "${target}" = "x86_64" ]; then
    abi="x86_64"
    toolchain="x86_64-clang"
    triplet="x64-android"
else
    echo "Error: ${target} was not recognized. Only arm64, arm and x86_64 are valid"
    echo "combinations."
    exit 1
fi

checkout_vcpkg "${gator_dir}" "${use_system_binaries}"

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
    build_path="${gator_dir}/build-${target}-${api_level}-${path_suffix}"
fi

export ANDROID_NDK_HOME="${ndk_path}"

if [ "${lto}" = "default" ]; then
    build_args=( -DCMAKE_ENABLE_LTO:BOOL=ON )
elif [ "${lto}" = "off" ]; then
    build_args=( -DCMAKE_ENABLE_LTO:BOOL=OFF )
else
    echo "Error: Invalid value for lto: '${lto}'"
    exit 1
fi

build_args+=( -DVCPKG_TARGET_TRIPLET="${triplet}" -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${gator_dir}/cmake/android.toolchain.cmake" -DCMAKE_BUILD_TYPE="${build_type}" -DANDROID_NDK="${ndk_path}" -DANDROID_ABI="${abi}" -DANDROID_TOOLCHAIN_NAME="${toolchain}" -DANDROID_PLATFORM="${api_level}" -DCONFIG_SUPPORT_PROC_POLLING=OFF -DCONFIG_PREFER_SYSTEM_WIDE_MODE=OFF -DCONFIG_ASSUME_PERF_HIGH_PARANOIA=OFF )

echo "Running cmake build for api_level=${api_level} with target=${target}:"
run_cmake "${cmake_exe}" "${cmake_generator}" "${src_path}" "${build_path}" "${use_system_binaries}" "${verbose}" "${build_args[@]}"

