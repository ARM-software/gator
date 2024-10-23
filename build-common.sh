# Check the provided CMake exe path, or provide one based on some default
validate_and_return_cmake_exe() {
    local exe_path="$1"
    if [ -z "${exe_path}" ]; then
        # prefer the same one vcpkg uses
        exe_path=`realpath "${gator_dir}"/vcpkg/downloads/tools/cmake-*/cmake-*/bin/cmake`
        if [ $(wc -l <<< "${exe_path}") = "1" ] && [ -f "${exe_path}" ] && [ -r "${exe_path}" ] && [ -x "${exe_path}" ]; then
            echo "${exe_path}"
        elif which cmake 1>/dev/null 2>&1; then
            which cmake
        else
            exit 1
        fi
    elif [ -f "${exe_path}" ] && [ -r "${exe_path}" ] && [ -x "${exe_path}" ]; then
        realpath "${exe_path}"
    else
        exit 1
    fi
}

# Check CMake generator and supply default
cmake_generator_or_default() {
    if [ -z "${1}" ]; then
        if which ninja 1>/dev/null 2>&1; then
            echo Ninja
        fi
    fi
    echo "${1}"
}

# Run the CMake build
run_cmake() {
    local cmake_exe="${1}"
    local cmake_generator="${2}"
    local src_path="${3}"
    local build_path="${4}"
    local use_system_binaries="${5}"
    local verbose="${6}"
    local build_args=( "${@:7}" )
    local last_result=0

    export VCPKG_DISABLE_METRICS=1
    if [ "${use_system_binaries}" = "y" ]; then
        export VCPKG_FORCE_SYSTEM_BINARIES=1
    fi

    if [ "${verbose}" = "y" ]; then
        build_args+=( -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON -DVCPKG_VERBOSE:BOOL=ON )
    fi

    if [ ! -z "${cmake_generator}" ]; then
        build_args+=( -G "${cmake_generator}" )
    fi

    echo "Entering ${build_path}"
    mkdir -p "${build_path}"
    cd "${build_path}"
    echo ""
    echo "Running cmake to configure the build:"
    echo "    ${cmake_exe} ${build_args[@]} ${src_path}"
    "${cmake_exe}" "${build_args[@]}" "${src_path}"
    last_result=$?
    [[ $last_result -ne 0 ]] && echo "Build failed. See command output for details." && exit $last_result
    echo ""
    echo "Running cmake to build:"
    echo "    ${cmake_exe} --build ${build_path}"
    "${cmake_exe}" --build "${build_path}"
    last_result=$?
    [[ $last_result -ne 0 ]] && echo "Build failed. See command output for details." && exit $last_result
    echo ""
    echo "Build complete. Please find gatord binaries at:"
    echo "    ${build_path}/gatord"
}

# Fetch vcpkg
checkout_vcpkg() {
    local vcpkg_commit="501db0f17ef6df184fcdbfbe0f87cde2313b6ab1" # upstream/2023.04.15
    local root="${1}"
    local use_system_binaries="${2}"
    local exe_path="${root}/vcpkg/vcpkg"
    # is gator git repo from github?
    if [ -d "${root}/.git" ]; then
        echo "Fetching vcpkg git submodule"
        git submodule update --init
    # checkout manually?
    elif [ ! -e "${root}/vcpkg/.git" ]; then
        echo "Cloning vcpkg git repo into '${root}/vcpkg/'"
        git clone https://github.com/microsoft/vcpkg.git "${root}/vcpkg/"
        echo "Switching to correct commit"
        git -C "${root}/vcpkg/" clean -ffxd
        git -C "${root}/vcpkg/" reset --hard "${vcpkg_commit}" --
    fi
    # Now bootstrap it?
    if [ ! -f "${exe_path}" ] || [ ! -r "${exe_path}" ] || [ ! -x "${exe_path}" ]; then
        echo "Bootstrapping vcpkg"
        if [ "${use_system_binaries}" = "y" ]; then
            export VCPKG_FORCE_SYSTEM_BINARIES=1
        fi
        "${root}/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
        last_result=$?
        if [[ $last_result -ne 0 ]]; then
            echo "Bootstrapping of vcpkg failed. Build cannot continue."
            exit $last_result
        fi
    fi
}

# Fetch perfetto
checkout_perfetto() {
    local perfetto_commit="7e8d6801dbf73936a916dbcd8ed06a758c8d989e" # v25.0
    local root="${1}"
    # is gator git repo from github?
    if [ -d "${root}/.git" ]; then
        echo "Fetching perfetto git submodule"
        git submodule update --init
    # checkout manually?
    elif [ ! -e "${root}/ext/perfetto/.git" ]; then
        echo "Cloning perfetto git repo into '${root}/ext/perfetto/'"
        git clone https://github.com/google/perfetto.git "${root}/ext/perfetto/"
        echo "Switching to correct commit"
        git -C "${root}/ext/perfetto/" clean -ffxd
        git -C "${root}/ext/perfetto/" reset --hard "${perfetto_commit}"
    fi
}

# source root
gator_dir=`dirname "$0"`
gator_dir=`realpath "${gator_dir}"`
src_path="${gator_dir}/daemon"

# setup some defaults
build_path=
build_type=Release
cmake_exe=
cmake_generator=
path_suffix=rel
verbose=n

if [ `uname -m` == "x86_64" ]; then
    use_system_binaries=n
else
    use_system_binaries=y
fi
