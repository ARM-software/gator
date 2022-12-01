# HWCPipe2 testing

## Overview

There is one entry point (one executable) to run all the tests -- the `hwcpipe_test` binary.
It includes two types of tests: unit test and end-to-end tests.

Unit tests are self contained. They do not rely on the kernel, hardware or any library.
Any external dependency is mocked. Thus, the test can be ran on any platform even if Mali is not supported.
Unit tests are tagged with a `[unit]` tag.

End-to-end tests, on the other hand, do rely on the kernel / hardware. They request
real device info / real counters and check that the information is piped to the user
space as expected. End-to-end tests are tagged with an `[end-to-end]` tag.

## Configuration and building

There are two options to enable the tests:

- `HWCPIPE_ENABLE_TESTS` enables unit tests and end-to-end tests if the target CPU is ARM.
  The option is enabled by default.
- `HWCPIPE_ENABLE_END_TO_END_TESTS` enables end-to-end tests explicitly for non ARM CPU architectures.
  The option is enabled by default for ARM CPUs.

For example, to build HWCPipe2 for GPU model with end-to-end tests enabled, run this command:

    ./build/cmake.py generate \
        --os=linux --toolchain=${TOOLCHAIN} \
        --build_type=${BUILDTYPE} \
        --arch=${ARCH} \
        -- \
        -DHWCPIPE_SYSCALL_LIBMALI=ON \
        -DHWCPIPE_ENABLE_END_TO_END_TESTS=ON

where:

- `${TOOLCHAIN}` is one of `gcc` or `clang`;
- `${BUILDTYPE}` is one of `release`, `profile`, or `debug`;
- `${ARCH}` is one of `x86`, `x64`.

## Running

Use `hwcpipe_test` executable to run all the tests enabled at configuration time:

    ./hwcpipe_test

Use `[unit]` or `[end-to-end]` tags to run only unit or end-to-end tests respectively:

    # For unit tests only:
    ./hwcpipe_test "[unit]"
    # For end-to-end tests only
    ./hwcpipe_test "[end-to-end]"

To run tests with the GPU model, you need to situate the test with libmali.so. Set `LD_LIBRRARY_PATH`
to the directory where `libmali.so` is. If your GPU is a CSF GPU, set `MALI_CSF_FIRMWARE_PATH` to
the `mali_csffw.bin` location. For example:

    LD_LIBRARY_PATH=$OUTDIR/install/lib MALI_CSF_FIRMWARE_PATH=$OUTDIR/install/bin/mali_csffw.bin ./hwcpipe_test
