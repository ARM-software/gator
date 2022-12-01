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
 * Workload interface header.
 */

#pragma once

#include <memory>

/** Vulkan workload class. */
class workload {
  public:
    using workload_ptr = std::unique_ptr<workload>;

    /**
     * Create a workload object.
     *
     * @return Workload instance created.
     */
    static workload_ptr create();

    /** Destroy Vulkan execution environment. */
    virtual ~workload() = default;

    /**
     * Start rendering synchronously in current thread.
     *
     * @param num_frames[in] Number of frames to render.
     */
    virtual void start(uint32_t num_frames) = 0;

    /**
     * Start rendering asynchronously in a separate thread.
     *
     * This shouldn't be called until the previous start_async() is completed.
     *
     * @param num_frames[in] Number of frames to render.
     */
    virtual void start_async(uint32_t num_frames) = 0;

    /** Stop asynchronous rendering. */
    virtual void stop_async() = 0;

    /** Wait until asynchronous rendering is finished. */
    virtual void wait_async_complete() = 0;

    /**
     * Check if async workload is fully rendered and completed.
     *
     * @return true if workload is completed.
     */
    virtual bool is_async_completed() = 0;

    /**
     * Set dump image flag.
     *
     * @param[in] flag    Dump image flag value to set.
     */
    virtual void set_dump_image(bool flag = true) = 0;

    /**
     * Check if at least one frame is rendered.
     *
     * @return true if at least one frame is rendered, otherwise false.
     */
    virtual bool check_rendered() = 0;
};
