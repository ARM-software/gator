# HWCPipe2 Contributing Guide

## Project structure

HWCPipe2 is designed to be modular for ease of maintenance, so each separable
component is its own small 'artifact'. Note those are not really meant to be
used externally because the intended end result of the build is an amalgamated
header + a single translation unit (see next section).

### Build system

The build system for the project is [CMake](https://cmake.org).
It _can_ be used to integrate the library into your own project, but the intent
is that it's only for developers' use. End users of the library should just use
the SDK that the build generates, since that makes it vastly simpler to
integrate into any pre-existing build.

#### Linters

The project uses the `pre-commit` tool to enforce usage of the linters below.

This is a (non-exhaustive) list of linters in use:

- `clang-format` for auto-formatting the code;
- `clang-tidy` for automatic discovery of bug prone code and modernisation
  opportunities;
- `commitlint` to enforce a suitable commit message standard;

### Directory layout

Each component has its own subdirectory, e.g.:

```shell-session
$ tree $(git rev-parse --show-toplevel)
/path/to/hwcpipe2
|-- CMakeLists.txt
|-- libcomponentA
|   |-- CMakeLists.txt
|   |-- include
|       |-- api.hpp
|       |-- public_header.hpp
|   |-- src
|       |-- private_header.hpp
|       |-- implementation.cpp
|   |-- test
|   |   |-- CMakeLists.txt
|       |-- private_header_test.cpp
|       |-- implementation_test.cpp
|-- libcomponentB
|   |-- CMakeLists.txt
|   |-- include
|       |-- api.hpp
|       |-- public_header.hpp
|   |-- src
|       |-- private_header.hpp
|       |-- implementation.cpp
|   |-- test
|   |   |-- CMakeLists.txt
|       |-- private_header_test.cpp
|       |-- implementation_test.cpp
```

With:

- The `include` subdirectory contains the public API of the component;
- The `src` subdirectory contains the implementation details of the component;
- The `test` subdirectory contains the unit/integration tests and/or
  benchmarks of the component;

The top `namespace` is always `hwcpipe`. Other `namespace`s should agree with
the directories structure, e.g. `libcomponentA/include/public_header.hpp`
should use `hwcpipe::libcomponenta` namespace.

### Documentation

Each component has a Doxygen-style page describing the purpose and high-level
usage expectations in its `api.hpp` header. Individual symbols are also
documented with in-line Doxygen **in the header**.

### Testing

The project uses [`catch2`](https://github.com/catchorg/Catch2) for its unit
tests and benchmarks. The aim is to have >90% coverage (line and branch) of the
unit tests.
