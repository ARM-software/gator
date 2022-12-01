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

add_executable(hwcpipe_test main.cpp)
target_link_libraries(
    hwcpipe_test
    catch2
    device_test
    debug_test
)
add_test(NAME hwcpipe_test COMMAND hwcpipe_test)
install(TARGETS hwcpipe_test)

if(HWCPIPE_ENABLE_END_TO_END_TESTS)
    add_subdirectory(workload)

    add_executable(workload_test workload_test.cpp)
    target_link_libraries(workload_test workload)
    install(TARGETS workload_test)

    add_subdirectory(end_to_end)
    target_link_libraries(hwcpipe_test end_to_end_test)
endif()
