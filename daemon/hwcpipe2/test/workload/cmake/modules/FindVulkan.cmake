#
# Copyright (c) 2022 ARM Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

include(FindPackageHandleStandardArgs)

if(ANDROID)
    # cmake-lint: disable=C0103
    find_path(
        Vulkan_INCLUDE_DIR
        NAMES vulkan/vulkan.hpp
        PATHS ${CMAKE_ANDROID_NDK}/sources/third_party/vulkan/src/include
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
    )
    set(Vulkan_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})
    find_package_handle_standard_args(Vulkan REQUIRED_VARS Vulkan_INCLUDE_DIR)
else()
    set(CMAKE_MODULE_PATH_BAK ${CMAKE_MODULE_PATH})
    set(CMAKE_MODULE_PATH "")
    find_package(Vulkan REQUIRED)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH_BAK})
endif()

find_program(
    Vulkan_GLSLC_EXECUTABLE
    glslc
    REQUIRED
)
