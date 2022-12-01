/*
 * Copyright (c) 2022 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file
 *
 * logger implementation.
 */

#pragma once

#if HWCPIPE_LOGGER_ENABLE
#ifdef ANDROID
#include <android/log.h>
#define LOG_DEBUG(x)                                                                                                   \
    do {                                                                                                               \
        std::ostringstream ss;                                                                                         \
        ss << x;                                                                                                       \
        __android_log_write(ANDROID_LOG_DEBUG, "hwcpipe", ss.str().c_str());                                           \
    } while (false)
#else
#include <iostream>
#define LOG_DEBUG(x)                                                                                                   \
    do {                                                                                                               \
        std::cout << __FILE__ << ":" << __LINE__ << ": \n" << x << "\n";                                               \
    } while (false)
#endif // ANDROID
#else
#define LOG_DEBUG(x)                                                                                                   \
    do {                                                                                                               \
    } while (false)
#endif // HWCPIPE_LOGGER_ENABLE
