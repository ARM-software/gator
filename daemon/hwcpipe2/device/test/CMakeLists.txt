#
# Copyright (c) 2021-2022 ARM Limited.
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

add_library(
    device_test OBJECT
    device/detail/enum_operators_test.cpp
    device/detail/is_empty_class_test.cpp
    device/handle_test.cpp
    device/hwcnt/backend_type_test.cpp
    device/hwcnt/block_iterator_test.cpp
    device/hwcnt/blocks_view_test.cpp
    device/hwcnt/sample_test.cpp
    device/hwcnt/sampler/filefd_guard_test.cpp
    device/hwcnt/sampler/filter_block_extents_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/backend_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/construct_block_extents_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/convert_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/block_index_remap_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/enum_info_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/metadata_parser_test.cpp
    device/hwcnt/sampler/kinstr_prfcnt/setup_test.cpp
    device/hwcnt/sampler/mapped_memory_test.cpp
    device/hwcnt/sampler/poll_test.cpp
    device/hwcnt/sampler/vinstr/backend_test.cpp
    device/hwcnt/sampler/vinstr/construct_block_extents_test.cpp
    device/hwcnt/sampler/vinstr/convert_test.cpp
    device/hwcnt/sampler/vinstr/queue_test.cpp
    device/hwcnt/sampler/vinstr/sample_layout_test.cpp
    device/hwcnt/sampler/vinstr/session_test.cpp
    device/hwcnt/sampler/vinstr/setup_test.cpp
    device/hwcnt/sampler/vinstr/timestamp_test.cpp
    device/ioctl/offset_pointer_test.cpp
    device/ioctl/pointer64_test.cpp
    device/ioctl/strided_array_test.cpp
    device/kbase_version_test.cpp
    device/product_id_test.cpp
    device/syscall/iface_test.cpp
)

target_include_directories(device_test PRIVATE .)

add_library(device_test_include INTERFACE)
target_include_directories(device_test_include INTERFACE include)

find_package(Threads)

target_link_libraries(
    device_test
    PUBLIC device_private
           device
           catch2
           ${CMAKE_THREAD_LIBS_INIT}
    PRIVATE device_test_include
)
