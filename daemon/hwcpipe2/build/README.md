# Prerequisites for building

Building HWCPipe2 requires the following tools:

- CMake 3.13.5 or later
- Ninja
- [Android-only] The Android NDK
- A C++14 capable compiler (either GCC or Clang for Linux; for Android HWCPipe2 uses the NDK-packaged Clang compiler)
- Python 3.8 or later

# Generating a build tree for HWCPipe2

## Linux

From the git tree toplevel, run:

    ./build/cmake.py generate --os=linux --toolchain=${TOOLCHAIN} --build-type=${BUILDTYPE} --arch=${ARCH}

where:

- `${TOOLCHAIN}` is one of `gcc` or `clang`;
- `${BUILDTYPE}` is one of `release`, `profile`, or `debug`;
- `${ARCH}` is one of `x86`, `x64`, `arm`, or `arm64`.

## Android

To build for Android, a working Android NDK is required and
`${ANDROID_NDK_HOME}` must be set. Then, from the git tree toplevel, run:

    ./build/cmake.py generate --os=android --toolchain=clang --build-type=${BUILDTYPE} --arch=${ARCH}

where:

- `${BUILDTYPE}` is one of `release`, `profile`, or `debug`;
- `${ARCH}` is one of `x86`, `x64`, `arm`, or `arm64`.

## HWCPipe2 for use with the GPU Model

To build for the model, you need a recent enough DDK that includes the syscall
symbol mocks. Then from the git tree toplevel:

    ./build/cmake.py generate --os=linux --toolchain=${TOOLCHAIN} --build_type=${BUILDTYPE} --arch=${ARCH} -- -DHWCPIPE_SYSCALL_LIBMALI=ON

where:

- `${TOOLCHAIN}` is one of `gcc` or `clang`;
- `${BUILDTYPE}` is one of `release`, `profile`, or `debug`;
- `${ARCH}` is one of `x86`, `x64`.

# Building

From the git tree toplevel, run:

    ninja -C out/${OS}_${TOOLCHAIN}_${BUILDTYPE}_${ARCH}

where:

- `${OS}` is one of `android` or `linux`;
- `${TOOLCHAIN}` is one of `gcc` or `clang`;
- `${BUILDTYPE}` is one of `release`, `profile`, or `debug`;
- `${ARCH}` is one of `x86`, `x64`, `arm`, or `arm64`.
- the build configuration has already been generated using the examples above

# Additional CMake build options

If, for example, you wish to build the tests that come with HWCPipe2, you can pass extra arguments to CMake like so:

    ./build/cmake.py generate --os=${OS} --toolchain=${TOOLCHAIN} --build-type=${BUILDTYPE} --arch=${ARCH} -- -DHWCPIPE_ENABLE_TESTS=ON

For the remainder of the configuration options, please refer to the top-level `CMakeLists.txt` file.
