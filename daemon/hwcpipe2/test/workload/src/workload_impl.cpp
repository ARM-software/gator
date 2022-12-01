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

#include "transform.hpp"
#include "workload_impl.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#define LOG(...) printf(__VA_ARGS__)

// Macro to check and display Vulkan return results
#define VK_CHECK_RESULT(f)                                                                                             \
    {                                                                                                                  \
        VkResult res = (f);                                                                                            \
        if (res != VK_SUCCESS) {                                                                                       \
            std::cout << "Fatal : VkResult is \"" << error_string(res) << "\" in " << __FILE__ << " at line "          \
                      << __LINE__ << "\n";                                                                             \
            assert(res == VK_SUCCESS);                                                                                 \
        }                                                                                                              \
    }

auto &vkd = VULKAN_HPP_DEFAULT_DISPATCHER;

#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

alignas(uint32_t) static const char vertex_shader[] = {
#include <triangle.vert.spv.h>
};

static_assert(sizeof(vertex_shader) % sizeof(uint32_t) == 0, "vertex_shader is not valid SPIRV code.");

alignas(uint32_t) static const char fragment_shader[] = {
#include <triangle.frag.spv.h>
};

static_assert(sizeof(fragment_shader) % sizeof(uint32_t) == 0, "fragment_shader is not valid SPIRV code.");

// Return an error code as a string
std::string workload_impl::error_string(VkResult error_code) {
    switch (error_code) {
#define STR(r)                                                                                                         \
    case VK_##r:                                                                                                       \
        return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}

// Selected a suitable supported depth format starting with 32 bit down to 16 bit
// Returns false if none of the depth formats in the list is supported by the device
VkBool32 workload_impl::get_supported_depth_format(VkPhysicalDevice physical_device, VkFormat *depth_format) {
    // Since all depth formats may be optional, we need to find a suitable depth format to use
    // Start with the highest precision packed format
    std::vector<VkFormat> depth_formats = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT,
                                           VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT,
                                           VK_FORMAT_D16_UNORM};

    for (auto &format : depth_formats) {
        VkFormatProperties format_props;
        vkd.vkGetPhysicalDeviceFormatProperties(physical_device, format, &format_props);
        // Format must support depth stencil attachment for optimal tiling
        if (format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            *depth_format = format;
            return true;
        }
    }

    return false;
}

// Insert an image memory barrier into the command buffer
void workload_impl::insert_image_memory_barrier(VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags src_access_mask,
                                                VkAccessFlags dst_access_mask, VkImageLayout old_image_layout,
                                                VkImageLayout new_image_layout, VkPipelineStageFlags src_stage_mask,
                                                VkPipelineStageFlags dst_stage_mask,
                                                VkImageSubresourceRange subresource_range) {
    VkImageMemoryBarrier image_memory_barrier{};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.srcAccessMask = src_access_mask;
    image_memory_barrier.dstAccessMask = dst_access_mask;
    image_memory_barrier.oldLayout = old_image_layout;
    image_memory_barrier.newLayout = new_image_layout;
    image_memory_barrier.image = image;
    image_memory_barrier.subresourceRange = subresource_range;

    vkd.vkCmdPipelineBarrier(cmdbuffer, src_stage_mask, dst_stage_mask, 0, 0, nullptr, 0, nullptr, 1,
                             &image_memory_barrier);
}

// Load a SPIR-V shader contents from char array.
VkShaderModule workload_impl::load_shader(const void *shader_source, size_t shader_source_size) {
    assert(shader_source_size > 0);

    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo module_create_info{};
    module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_create_info.codeSize = shader_source_size;
    module_create_info.pCode = static_cast<const uint32_t *>(shader_source);

    VK_CHECK_RESULT(vkd.vkCreateShaderModule(device_, &module_create_info, nullptr, &shader_module));

    return shader_module;
}

workload_impl::workload_impl() {
#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
    static vk::DynamicLoader dl;
    // NOLINTNEXTLINE(readability-identifier-naming)
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    vkd.init(vkGetInstanceProcAddr);
#endif

    LOG("Running headless rendering example\n");
    create_instance();

    create_device();
    prepare_vertex_index_buffers();
    create_frambuffer_attachments();
    create_renderpass();
    prepare_graphics_pipeline();
}

workload_impl::~workload_impl() {
    if (thread_.joinable())
        thread_.join();

    vkd.vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vkd.vkFreeMemory(device_, vertex_memory_, nullptr);
    vkd.vkDestroyBuffer(device_, index_buffer_, nullptr);
    vkd.vkFreeMemory(device_, index_memory_, nullptr);
    vkd.vkDestroyImageView(device_, color_attachment_.view, nullptr);
    vkd.vkDestroyImage(device_, color_attachment_.image, nullptr);
    vkd.vkFreeMemory(device_, color_attachment_.memory, nullptr);
    vkd.vkDestroyImageView(device_, depth_attachment_.view, nullptr);
    vkd.vkDestroyImage(device_, depth_attachment_.image, nullptr);
    vkd.vkFreeMemory(device_, depth_attachment_.memory, nullptr);
    vkd.vkDestroyRenderPass(device_, render_pass_, nullptr);
    vkd.vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    vkd.vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkd.vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    vkd.vkDestroyPipeline(device_, pipeline_, nullptr);
    vkd.vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
    vkd.vkDestroyCommandPool(device_, command_pool_, nullptr);
    for (auto shadermodule : shader_modules_) {
        vkd.vkDestroyShaderModule(device_, shadermodule, nullptr);
    }
    vkd.vkDestroyDevice(device_, nullptr);
    vkd.vkDestroyInstance(instance_, nullptr);
}

void workload_impl::start(uint32_t num_frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto last_frame = num_frames + current_frame_;

    while (current_frame_ < last_frame)
        draw_frame(current_frame_++);
}

void workload_impl::start_async(uint32_t num_frames) {
    done_ = false;
    thread_ = std::thread([=]() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto last_frame = num_frames + current_frame_;
        while (current_frame_ < last_frame) {
            if (done_)
                break;
            draw_frame(current_frame_++);
        }
        done_ = true;
    });
}

void workload_impl::stop_async() { done_ = true; }

void workload_impl::wait_async_complete() {
    if (thread_.joinable())
        thread_.join();
}

bool workload_impl::check_rendered() {
    bool result = false;
    if (rendered_) {
        result = true;
        rendered_ = false;
    }
    return result;
}

void workload_impl::draw_frame(uint32_t frame_no) {
    LOG("Frame no: %u\n", frame_no);

    uint32_t rotate_degree = (10 * frame_no) % 360;
    prepare_command_buffer(rotate_degree);

    submit_work(command_buffer_, queue_);
    vkd.vkDeviceWaitIdle(device_);

    // Copy framebuffer image to host visible image
    if (dump_image_) {
        std::string filename = "headless" + std::to_string(frame_no) + ".ppm";
        dump_framebuffer_image(filename);
    }

    vkd.vkQueueWaitIdle(queue_);

    rendered_ = true;
}

uint32_t workload_impl::get_memory_type_index(uint32_t type_bits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkd.vkGetPhysicalDeviceMemoryProperties(physical_device_, &device_memory_properties);
    for (uint32_t i = 0; i < device_memory_properties.memoryTypeCount; i++) {
        if ((type_bits & 1) == 1) {
            if ((device_memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        type_bits >>= 1;
    }
    return 0;
}

VkResult workload_impl::create_buffer(VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags,
                                      VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data) {
    // Create the buffer handle
    VkBufferCreateInfo buffer_create_info{};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.usage = usage_flags;
    buffer_create_info.size = size;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(vkd.vkCreateBuffer(device_, &buffer_create_info, nullptr, buffer));

    // Create the memory backing up the buffer handle
    VkMemoryRequirements mem_reqs;

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    vkd.vkGetBufferMemoryRequirements(device_, *buffer, &mem_reqs);
    mem_alloc.allocationSize = mem_reqs.size;
    mem_alloc.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits, memory_property_flags);
    VK_CHECK_RESULT(vkd.vkAllocateMemory(device_, &mem_alloc, nullptr, memory));

    if (data != nullptr) {
        void *mapped = nullptr;
        VK_CHECK_RESULT(vkd.vkMapMemory(device_, *memory, 0, size, 0, &mapped));
        memcpy(mapped, data, size);
        vkd.vkUnmapMemory(device_, *memory);
    }

    VK_CHECK_RESULT(vkd.vkBindBufferMemory(device_, *buffer, *memory, 0));

    return VK_SUCCESS;
}

// Submit command buffer to a queue and wait for fence until queue operations have been finished
void workload_impl::submit_work(VkCommandBuffer cmd_buffer, VkQueue queue) {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffer;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = 0;

    VkFence fence = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkd.vkCreateFence(device_, &fence_info, nullptr, &fence));
    VK_CHECK_RESULT(vkd.vkQueueSubmit(queue, 1, &submit_info, fence));
    VK_CHECK_RESULT(vkd.vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX));
    vkd.vkDestroyFence(device_, fence, nullptr);
}

// Create VkInstance
void workload_impl::create_instance() {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Vulkan headless example";
    app_info.pEngineName = "workload_impl";
    app_info.apiVersion = VK_API_VERSION_1_0;

    // Vulkan instance creation (without surface extensions)
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &app_info;

    VK_CHECK_RESULT(vkd.vkCreateInstance(&instance_create_info, nullptr, &instance_));

#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
    // initialize function pointers for instance
    vkd.init(vk::Instance(instance_));
#endif
}

// Vulkan device creation
void workload_impl::create_device() {
    uint32_t device_count = 0;
    VK_CHECK_RESULT(vkd.vkEnumeratePhysicalDevices(instance_, &device_count, nullptr));
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    VK_CHECK_RESULT(vkd.vkEnumeratePhysicalDevices(instance_, &device_count, physical_devices.data()));
    physical_device_ = physical_devices[0];

    VkPhysicalDeviceProperties device_properties;
    vkd.vkGetPhysicalDeviceProperties(physical_device_, &device_properties);
    LOG("GPU: %s\n", device_properties.deviceName);

    // Request a single graphics queue
    const float default_queue_priority(0.0f);
    VkDeviceQueueCreateInfo queue_create_info = {};
    uint32_t queue_family_count = 0;
    vkd.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
    vkd.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_family_properties.data());

    const uint32_t num_queue_family_props = static_cast<uint32_t>(queue_family_properties.size());
    uint32_t queue_family_index = num_queue_family_props;

    for (uint32_t i = 0; i < num_queue_family_props; i++) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = i;
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = i;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &default_queue_priority;
            break;
        }
    }

    assert(queue_family_index < num_queue_family_props);

    // Create logical device
    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    VK_CHECK_RESULT(vkd.vkCreateDevice(physical_device_, &device_create_info, nullptr, &device_));

    // Get a graphics queue
    vkd.vkGetDeviceQueue(device_, queue_family_index, 0, &queue_);

    // Command pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queue_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_RESULT(vkd.vkCreateCommandPool(device_, &cmd_pool_info, nullptr, &command_pool_));
}

// Prepare vertex and index buffers
void workload_impl::prepare_vertex_index_buffers() {
    std::vector<vertex> vertices = {{{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                                    {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    std::vector<uint32_t> indices = {0, 1, 2};

    const auto vertex_buffer_size = static_cast<VkDeviceSize>(vertices.size()) * sizeof(vertex);
    const auto index_buffer_size = static_cast<VkDeviceSize>(indices.size()) * sizeof(uint32_t);

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    // Command buffer for copy commands (reused)
    VkCommandBufferAllocateInfo cmd_buf_allocate_info{};
    cmd_buf_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_allocate_info.commandPool = command_pool_;
    cmd_buf_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_allocate_info.commandBufferCount = 1;
    VkCommandBuffer copy_cmd = nullptr;
    VK_CHECK_RESULT(vkd.vkAllocateCommandBuffers(device_, &cmd_buf_allocate_info, &copy_cmd));

    VkCommandBufferBeginInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    // Copy input data to VRAM using a staging buffer

    // Vertices
    create_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_buffer,
                  &staging_memory, vertex_buffer_size, vertices.data());

    create_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertex_buffer_, &vertex_memory_, vertex_buffer_size);

    VK_CHECK_RESULT(vkd.vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
    VkBufferCopy copy_region = {};
    copy_region.size = vertex_buffer_size;
    vkd.vkCmdCopyBuffer(copy_cmd, staging_buffer, vertex_buffer_, 1, &copy_region);
    VK_CHECK_RESULT(vkd.vkEndCommandBuffer(copy_cmd));

    submit_work(copy_cmd, queue_);

    vkd.vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkd.vkFreeMemory(device_, staging_memory, nullptr);

    // Indices
    create_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_buffer,
                  &staging_memory, index_buffer_size, indices.data());

    create_buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &index_buffer_, &index_memory_, index_buffer_size);

    VK_CHECK_RESULT(vkd.vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
    copy_region.size = index_buffer_size;
    vkd.vkCmdCopyBuffer(copy_cmd, staging_buffer, index_buffer_, 1, &copy_region);
    VK_CHECK_RESULT(vkd.vkEndCommandBuffer(copy_cmd));

    submit_work(copy_cmd, queue_);

    vkd.vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkd.vkFreeMemory(device_, staging_memory, nullptr);
}

// Create framebuffer attachments
void workload_impl::create_frambuffer_attachments() {
    width_ = 64;
    height_ = 64;

    get_supported_depth_format(physical_device_, &depth_format_);

    // Color attachment
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = color_format_;
    image.extent.width = width_;
    image.extent.height = height_;
    image.extent.depth = 1;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    VkMemoryRequirements mem_reqs;

    VK_CHECK_RESULT(vkd.vkCreateImage(device_, &image, nullptr, &color_attachment_.image));
    vkd.vkGetImageMemoryRequirements(device_, color_attachment_.image, &mem_reqs);
    mem_alloc.allocationSize = mem_reqs.size;
    mem_alloc.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkd.vkAllocateMemory(device_, &mem_alloc, nullptr, &color_attachment_.memory));
    VK_CHECK_RESULT(vkd.vkBindImageMemory(device_, color_attachment_.image, color_attachment_.memory, 0));

    VkImageViewCreateInfo color_image_view{};
    color_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    color_image_view.format = color_format_;
    color_image_view.subresourceRange = {};
    color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color_image_view.subresourceRange.baseMipLevel = 0;
    color_image_view.subresourceRange.levelCount = 1;
    color_image_view.subresourceRange.baseArrayLayer = 0;
    color_image_view.subresourceRange.layerCount = 1;
    color_image_view.image = color_attachment_.image;
    VK_CHECK_RESULT(vkd.vkCreateImageView(device_, &color_image_view, nullptr, &color_attachment_.view));

    // Depth stencil attachment
    image.format = depth_format_;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK_RESULT(vkd.vkCreateImage(device_, &image, nullptr, &depth_attachment_.image));
    vkd.vkGetImageMemoryRequirements(device_, depth_attachment_.image, &mem_reqs);
    mem_alloc.allocationSize = mem_reqs.size;
    mem_alloc.memoryTypeIndex = get_memory_type_index(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkd.vkAllocateMemory(device_, &mem_alloc, nullptr, &depth_attachment_.memory));
    VK_CHECK_RESULT(vkd.vkBindImageMemory(device_, depth_attachment_.image, depth_attachment_.memory, 0));

    VkImageViewCreateInfo depth_stencil_view{};
    depth_stencil_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_stencil_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_stencil_view.format = depth_format_;
    depth_stencil_view.flags = 0;
    depth_stencil_view.subresourceRange = {};
    depth_stencil_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depth_stencil_view.subresourceRange.baseMipLevel = 0;
    depth_stencil_view.subresourceRange.levelCount = 1;
    depth_stencil_view.subresourceRange.baseArrayLayer = 0;
    depth_stencil_view.subresourceRange.layerCount = 1;
    depth_stencil_view.image = depth_attachment_.image;
    VK_CHECK_RESULT(vkd.vkCreateImageView(device_, &depth_stencil_view, nullptr, &depth_attachment_.view));
}

// Create renderpass
void workload_impl::create_renderpass() {
    std::array<VkAttachmentDescription, 2> attchment_descriptions = {};
    // Color attachment
    attchment_descriptions[0].format = color_format_;
    attchment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attchment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attchment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attchment_descriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attchment_descriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attchment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attchment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    // Depth attachment
    attchment_descriptions[1].format = depth_format_;
    attchment_descriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attchment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attchment_descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attchment_descriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attchment_descriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attchment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attchment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_reference = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass_description = {};
    subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.colorAttachmentCount = 1;
    subpass_description.pColorAttachments = &color_reference;
    subpass_description.pDepthStencilAttachment = &depth_reference;

    // Use subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Create the actual renderpass
    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attchment_descriptions.size());
    render_pass_info.pAttachments = attchment_descriptions.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass_description;
    render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    render_pass_info.pDependencies = dependencies.data();
    VK_CHECK_RESULT(vkd.vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_));

    VkImageView attachments[2];
    attachments[0] = color_attachment_.view;
    attachments[1] = depth_attachment_.view;

    VkFramebufferCreateInfo framebuffer_create_info{};
    framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_create_info.renderPass = render_pass_;
    framebuffer_create_info.attachmentCount = 2;
    framebuffer_create_info.pAttachments = attachments;
    framebuffer_create_info.width = width_;
    framebuffer_create_info.height = height_;
    framebuffer_create_info.layers = 1;
    VK_CHECK_RESULT(vkd.vkCreateFramebuffer(device_, &framebuffer_create_info, nullptr, &framebuffer_));
}

// Prepare graphics pipeline
void workload_impl::prepare_graphics_pipeline() {
    std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {};

    VkDescriptorSetLayoutCreateInfo descriptor_layout{};
    descriptor_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout.pBindings = set_layout_bindings.data();
    descriptor_layout.bindingCount = static_cast<uint32_t>(set_layout_bindings.size());
    VK_CHECK_RESULT(vkd.vkCreateDescriptorSetLayout(device_, &descriptor_layout, nullptr, &descriptor_set_layout_));

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 0;
    pipeline_layout_create_info.pSetLayouts = nullptr;

    // MVP via push constant block
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(mat4);
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    VK_CHECK_RESULT(vkd.vkCreatePipelineLayout(device_, &pipeline_layout_create_info, nullptr, &pipeline_layout_));

    VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
    pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VK_CHECK_RESULT(vkd.vkCreatePipelineCache(device_, &pipeline_cache_create_info, nullptr, &pipeline_cache_));

    // Create pipeline
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.flags = 0;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode = VK_CULL_MODE_NONE;
    rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state.flags = 0;
    rasterization_state.depthClampEnable = VK_FALSE;
    rasterization_state.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blend_attachment_state{};
    blend_attachment_state.colorWriteMask = 0xf;
    blend_attachment_state.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &blend_attachment_state;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.flags = 0;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.flags = 0;

    std::vector<VkDynamicState> dynamic_state_enables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pDynamicStates = dynamic_state_enables.data();
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_state_enables.size());
    dynamic_state.flags = 0;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.layout = pipeline_layout_;
    pipeline_create_info.renderPass = render_pass_;
    pipeline_create_info.flags = 0;
    pipeline_create_info.basePipelineIndex = -1;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{};

    pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    pipeline_create_info.pRasterizationState = &rasterization_state;
    pipeline_create_info.pColorBlendState = &color_blend_state;
    pipeline_create_info.pMultisampleState = &multisample_state;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_create_info.pStages = shader_stages.data();

    // vertex bindings an attributes
    // Binding description
    VkVertexInputBindingDescription v_input_bind_description{};
    v_input_bind_description.binding = 0;
    v_input_bind_description.stride = sizeof(vertex);
    v_input_bind_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputBindingDescription> vertex_input_bindings = {v_input_bind_description};

    // Attribute descriptions
    VkVertexInputAttributeDescription v_input_attrib_description_position{};
    v_input_attrib_description_position.location = 0;
    v_input_attrib_description_position.binding = 0;
    v_input_attrib_description_position.format = VK_FORMAT_R32G32B32_SFLOAT;
    v_input_attrib_description_position.offset = 0;

    VkVertexInputAttributeDescription v_input_attrib_description_color{};
    v_input_attrib_description_color.location = 1;
    v_input_attrib_description_color.binding = 0;
    v_input_attrib_description_color.format = VK_FORMAT_R32G32B32_SFLOAT;
    v_input_attrib_description_color.offset = sizeof(float) * 3;

    std::vector<VkVertexInputAttributeDescription> vertex_input_attributes = {
        v_input_attrib_description_position, // Position
        v_input_attrib_description_color,    // Color
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_bindings.size());
    vertex_input_state.pVertexBindingDescriptions = vertex_input_bindings.data();
    vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attributes.size());
    vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes.data();

    pipeline_create_info.pVertexInputState = &vertex_input_state;

    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].pName = "main";
    shader_stages[0].module = load_shader(vertex_shader, sizeof(vertex_shader));
    shader_stages[1].module = load_shader(fragment_shader, sizeof(fragment_shader));
    shader_modules_ = {shader_stages[0].module, shader_stages[1].module};
    VK_CHECK_RESULT(
        vkd.vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &pipeline_create_info, nullptr, &pipeline_));
}

// Prepare Command buffer
void workload_impl::prepare_command_buffer(uint32_t rotate_degree) {
    VkCommandBufferAllocateInfo cmd_buf_allocate_info{};
    cmd_buf_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_allocate_info.commandPool = command_pool_;
    cmd_buf_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_allocate_info.commandBufferCount = 1;
    VK_CHECK_RESULT(vkd.vkAllocateCommandBuffers(device_, &cmd_buf_allocate_info, &command_buffer_));

    VkCommandBufferBeginInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT(vkd.vkBeginCommandBuffer(command_buffer_, &cmd_buf_info));

    VkClearValue clear_values[2];
    clear_values[0].color = {{0.0f, 0.0f, 0.2f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_begin_info = {};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderArea.extent.width = width_;
    render_pass_begin_info.renderArea.extent.height = height_;
    render_pass_begin_info.clearValueCount = 2;
    render_pass_begin_info.pClearValues = clear_values;
    render_pass_begin_info.renderPass = render_pass_;
    render_pass_begin_info.framebuffer = framebuffer_;

    vkd.vkCmdBeginRenderPass(command_buffer_, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.height = (float)height_;
    viewport.width = (float)width_;
    viewport.minDepth = (float)0.0f;
    viewport.maxDepth = (float)1.0f;
    vkd.vkCmdSetViewport(command_buffer_, 0, 1, &viewport);

    // Update dynamic scissor state
    VkRect2D scissor = {};
    scissor.extent.width = width_;
    scissor.extent.height = height_;
    vkd.vkCmdSetScissor(command_buffer_, 0, 1, &scissor);

    vkd.vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Render scene
    VkDeviceSize offsets[1] = {0};
    vkd.vkCmdBindVertexBuffers(command_buffer_, 0, 1, &vertex_buffer_, offsets);
    vkd.vkCmdBindIndexBuffer(command_buffer_, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

    // Position vectors
    std::vector<vec4> pos = {
        // (x, y, z)
        vec4{-1.5f, 0.0f, -4.0f},
        vec4{0.0f, 0.0f, -2.5f},
        vec4{1.5f, 0.0f, -4.0f},
    };

    mat4 identity = {
        vec4{1.0f, 0.0f, 0.0f, 0.0f}, //
        vec4{0.0f, 1.0f, 0.0f, 0.0f}, //
        vec4{0.0f, 0.0f, 1.0f, 0.0f}, //
        vec4{0.0f, 0.0f, 0.0f, 1.0f}, //
    };

    float angle = radians(static_cast<float>(rotate_degree));
    for (auto v : pos) {
        mat4 translated = translate(identity, v);
        mat4 rotated = rotate_y(translated, angle);
        mat4 perspectived = perspective(radians(60.0f), (float)width_ / (float)height_, 0.1f, 256.0f);
        mat4 mvp_matrix = perspectived * rotated;
        vkd.vkCmdPushConstants(command_buffer_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp_matrix),
                               &mvp_matrix);
        vkd.vkCmdDrawIndexed(command_buffer_, 3, 1, 0, 0, 0);
    }

    vkd.vkCmdEndRenderPass(command_buffer_);

    VK_CHECK_RESULT(vkd.vkEndCommandBuffer(command_buffer_));
}

void workload_impl::dump_framebuffer_image(const std::string &filename) {
    // Create the linear tiled destination image to copy to and to read the memory from
    VkImageCreateInfo img_create_info{};
    img_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_create_info.imageType = VK_IMAGE_TYPE_2D;
    img_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_create_info.extent.width = width_;
    img_create_info.extent.height = height_;
    img_create_info.extent.depth = 1;
    img_create_info.arrayLayers = 1;
    img_create_info.mipLevels = 1;
    img_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_create_info.tiling = VK_IMAGE_TILING_LINEAR;
    img_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // Create the image
    VkImage dst_image = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkd.vkCreateImage(device_, &img_create_info, nullptr, &dst_image));
    // Create memory to back up the image
    VkMemoryRequirements mem_requirements;

    VkMemoryAllocateInfo mem_alloc_info{};
    mem_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    VkDeviceMemory dst_image_memory = VK_NULL_HANDLE;
    vkd.vkGetImageMemoryRequirements(device_, dst_image, &mem_requirements);
    mem_alloc_info.allocationSize = mem_requirements.size;
    // Memory must be host visible to copy from
    mem_alloc_info.memoryTypeIndex = get_memory_type_index(
        mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(vkd.vkAllocateMemory(device_, &mem_alloc_info, nullptr, &dst_image_memory));
    VK_CHECK_RESULT(vkd.vkBindImageMemory(device_, dst_image, dst_image_memory, 0));

    // Do the actual blit from the offscreen image to our host visible destination image
    VkCommandBufferAllocateInfo cmd_buf_allocate_info{};
    cmd_buf_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_allocate_info.commandPool = command_pool_;
    cmd_buf_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_allocate_info.commandBufferCount = 1;
    VkCommandBuffer copy_cmd = nullptr;
    VK_CHECK_RESULT(vkd.vkAllocateCommandBuffers(device_, &cmd_buf_allocate_info, &copy_cmd));

    VkCommandBufferBeginInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT(vkd.vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));

    // Transition destination image to transfer destination layout
    insert_image_memory_barrier(copy_cmd, dst_image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    // color_attachment.image is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned

    VkImageCopy image_copy_region{};
    image_copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy_region.srcSubresource.layerCount = 1;
    image_copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy_region.dstSubresource.layerCount = 1;
    image_copy_region.extent.width = width_;
    image_copy_region.extent.height = height_;
    image_copy_region.extent.depth = 1;

    vkd.vkCmdCopyImage(copy_cmd, color_attachment_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy_region);

    // Transition destination image to general layout, which is the required layout for mapping the image memory later
    // on
    insert_image_memory_barrier(copy_cmd, dst_image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    VK_CHECK_RESULT(vkd.vkEndCommandBuffer(copy_cmd));

    submit_work(copy_cmd, queue_);

    // Get layout of the image (including row pitch)
    VkImageSubresource sub_resource{};
    sub_resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout sub_resource_layout;

    vkd.vkGetImageSubresourceLayout(device_, dst_image, &sub_resource, &sub_resource_layout);

    // Map image memory so we can start copying from it
    void *imagedata_raw = nullptr;
    vkd.vkMapMemory(device_, dst_image_memory, 0, VK_WHOLE_SIZE, 0, &imagedata_raw);
    const char *imagedata = static_cast<const char *>(imagedata_raw);

    imagedata += sub_resource_layout.offset;

    // Save host visible framebuffer image to disk (ppm format)

    std::ofstream file(filename, std::ios::out | std::ios::binary);

    // ppm header
    file << "P6\n" << width_ << "\n" << height_ << "\n" << 255 << "\n";

    // If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have
    // to manually swizzle color components Check if source is BGR and needs swizzle
    std::vector<VkFormat> formats_bgr = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM};
    const bool color_swizzle =
        (std::find(formats_bgr.begin(), formats_bgr.end(), VK_FORMAT_R8G8B8A8_UNORM) != formats_bgr.end());

    // ppm binary pixel data
    for (int32_t y = 0; y < height_; y++) {
        auto row = imagedata;
        for (int32_t x = 0; x < width_; x++) {
            if (color_swizzle) {
                file.write(row + 2, 1);
                file.write(row + 1, 1);
                file.write(row, 1);
            } else {
                file.write(row, 3);
            }
            row += 4;
        }
        imagedata += sub_resource_layout.rowPitch;
    }
    file.close();

    LOG("Framebuffer image saved to %s\n", filename.c_str());

    // Clean up resources
    vkd.vkUnmapMemory(device_, dst_image_memory);
    vkd.vkFreeMemory(device_, dst_image_memory, nullptr);
    vkd.vkDestroyImage(device_, dst_image, nullptr);
}
