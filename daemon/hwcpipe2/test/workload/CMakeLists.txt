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

# Set policy to add ${<PackageName>_ROOT} variable to find_package search path.
cmake_policy(
    SET
    CMP0074
    NEW
)

list(
    APPEND
    CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)
find_package(Vulkan REQUIRED)

find_package(Threads)

add_custom_command(
    OUTPUT triangle.vert.spv.h
    BYPRODUCTS triangle.vert.spv
    COMMAND
        ${Vulkan_GLSLC_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert -o
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
    COMMAND
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.py
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv.h
    DEPENDS shaders/triangle.vert tools/bin2c.py
    COMMENT "Generating triangle.vert.spv.h"
    VERBATIM
)

add_custom_command(
    OUTPUT triangle.frag.spv.h
    BYPRODUCTS triangle.frag.spv
    COMMAND
        ${Vulkan_GLSLC_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag -o
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
    COMMAND
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.py
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
        ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv.h
    DEPENDS shaders/triangle.frag tools/bin2c.py
    COMMENT "Generating triangle.frag.spv.h"
    VERBATIM
)

add_custom_target(
    workload_shaders
    DEPENDS triangle.vert.spv.h triangle.frag.spv.h
    COMMENT "Building workload_shaders"
)

add_library(
    workload STATIC
    src/workload.cpp
    src/workload_impl.cpp
    src/transform.cpp
)

target_include_directories(
    workload
    PUBLIC include
    PRIVATE ${Vulkan_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_options(workload PRIVATE "-fexceptions")

target_link_libraries(
    workload
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
)

add_dependencies(workload workload_shaders)
