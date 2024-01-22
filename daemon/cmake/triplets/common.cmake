# Copyright (C) 2022-2023 by Arm Limited (or its affiliates). All rights reserved.

# C++17 should be enabled for all targets
set(_VCPKG_CMAKE_CXX_FLAGS  "-std=gnu++17 -fexceptions -frtti -fvisibility=hidden")

# Configure the default compiler options
set(VCPKG_CMAKE_CONFIGURE_OPTIONS   "-DCMAKE_CXX_FLAGS:STRING=${_VCPKG_CMAKE_CXX_FLAGS}"
                                    "-DCMAKE_CXX_STANDARD:STRING=17"
                                    "-DCMAKE_CXX_STANDARD_REQUIRED:BOOL=ON")
