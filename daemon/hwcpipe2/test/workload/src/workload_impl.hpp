/*
 * Copyright (c) 2022 ARM Limited.
 * Copyright (c) 2017 by Sascha Willems - www.saschawillems.de
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
 *
 * This code is modified based on the following examples.
 *   https://github.com/SaschaWillems/Vulkan/blob/master/examples/renderheadless/renderheadless.cpp
 *   https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanTools.h
 *   https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanTools.cpp
 *
 * The original code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <workload/workload.hpp>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class workload_impl : public workload {
    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkPipelineCache pipeline_cache_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkCommandBuffer command_buffer_{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout_{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    std::vector<VkShaderModule> shader_modules_;
    VkBuffer vertex_buffer_{VK_NULL_HANDLE};
    VkBuffer index_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory vertex_memory_{VK_NULL_HANDLE};
    VkDeviceMemory index_memory_{VK_NULL_HANDLE};

    struct frame_buffer_attachment {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
    };
    int32_t width_{};
    int32_t height_{};
    VkFramebuffer framebuffer_{VK_NULL_HANDLE};
    frame_buffer_attachment color_attachment_{};
    frame_buffer_attachment depth_attachment_{};
    VkRenderPass render_pass_{VK_NULL_HANDLE};

    struct vertex {
        float position[3];
        float color[3];
    };

    VkFormat color_format_{VK_FORMAT_R8G8B8A8_UNORM};
    VkFormat depth_format_{VK_FORMAT_D16_UNORM};

    std::thread thread_{};
    std::mutex mutex_{};
    std::atomic<bool> done_{};
    uint32_t current_frame_{};
    bool dump_image_{};

    std::atomic<bool> rendered_{false};

    // Return an error code as a string
    std::string error_string(VkResult error_code);

    // Selected a suitable supported depth format starting with 32 bit down to 16 bit
    // Returns false if none of the depth formats in the list is supported by the device
    VkBool32 get_supported_depth_format(VkPhysicalDevice physical_device, VkFormat *depth_format);

    // Insert an image memory barrier into the command buffer
    void insert_image_memory_barrier(VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags src_access_mask,
                                     VkAccessFlags dst_access_mask, VkImageLayout old_image_layout,
                                     VkImageLayout new_image_layout, VkPipelineStageFlags src_stage_mask,
                                     VkPipelineStageFlags dst_stage_mask, VkImageSubresourceRange subresource_range);

    // Load a SPIR-V shader contents from char array.
    VkShaderModule load_shader(const void *shader_source, size_t shader_source_size);

  public:
    // Initialize Vulkan execution environment until preparing graphics pipeline.
    workload_impl();

    // Destroy Vulkan execution environment.
    ~workload_impl();

    // Start rendering synchronously in current thread.
    void start(uint32_t num_frames) override;

    // Start rendering asynchronously in a separate thread.
    // This shouldn't be called until the previous start_async() is completed.
    void start_async(uint32_t num_frames) override;

    // Stop asynchronous rendering.
    void stop_async() override;

    // Wait until asynchronous rendering is finished.
    void wait_async_complete() override;

    // Check if async workload is fully rendered and completed.
    bool is_async_completed() override { return done_; }

    // Set dump image flag.
    void set_dump_image(bool flag = true) override { dump_image_ = flag; }

    // Check if at least one frame is rendered.
    bool check_rendered() override;

  private:
    // Draw rendering for the given frame number in the current thread.
    void draw_frame(uint32_t frame_no);

    uint32_t get_memory_type_index(uint32_t type_bits, VkMemoryPropertyFlags properties);

    VkResult create_buffer(VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags,
                           VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr);

    // Submit command buffer to a queue and wait for fence until queue operations have been finished
    void submit_work(VkCommandBuffer cmd_buffer, VkQueue queue);

    // Create VkInstance
    void create_instance();

    // Vulkan device creation
    void create_device();

    // Prepare vertex and index buffers
    void prepare_vertex_index_buffers();

    // Create framebuffer attachments
    void create_frambuffer_attachments();

    // Create renderpass
    void create_renderpass();

    // Prepare graphics pipeline
    void prepare_graphics_pipeline();

    // Prepare Command buffer
    void prepare_command_buffer(uint32_t rotate_degree);

    void dump_framebuffer_image(const std::string &filename);
};
