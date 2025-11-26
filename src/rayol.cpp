#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <limits>

#if RAYOL_ENABLE_VULKAN
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "ui/imgui_layer.h"
#if RAYOL_USE_IMGUI
#include <imgui.h>
#endif
#endif

namespace rayol {

#if RAYOL_ENABLE_VULKAN

class VulkanContext {
public:
    bool init(SDL_Window* window) {
        window_ = window;
        if (!create_instance()) return false;
        if (!create_surface()) return false;
        if (!pick_physical_device()) return false;
        if (!create_device()) return false;
        if (!create_descriptor_pool()) return false;
        if (!create_swapchain()) return false;
        if (!create_image_views()) return false;
        if (!create_render_pass()) return false;
        if (!create_framebuffers()) return false;
        if (!create_command_pool()) return false;
        if (!create_sync_objects()) return false;
        if (!allocate_command_buffers()) return false;
        return true;
    }

    void set_imgui_layer(ImGuiLayer* layer) { imgui_layer_ = layer; }

    bool draw_frame(bool& should_close_ui) {
        vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                 image_available_[current_frame_], VK_NULL_HANDLE,
                                                 &image_index);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreate_swapchain()) return false;
            return true;
        } else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            std::cerr << "Failed to acquire swapchain image." << std::endl;
            return false;
        }

        if (images_in_flight_[image_index] != VK_NULL_HANDLE) {
            vkWaitForFences(device_, 1, &images_in_flight_[image_index], VK_TRUE, UINT64_MAX);
        }
        images_in_flight_[image_index] = in_flight_fences_[current_frame_];

        vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

        vkResetCommandBuffer(command_buffers_[image_index], 0);

#if RAYOL_USE_IMGUI
        if (imgui_layer_) {
            imgui_layer_->begin_frame();
            ImGui::Begin("Rayol UI");
            if (ImGui::Button("Exit")) {
                should_close_ui = true;
            }
            ImGui::End();
        }
#endif

        record_commands(command_buffers_[image_index], image_index);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available_[current_frame_];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers_[image_index];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished_[current_frame_];

        if (vkQueueSubmit(queue_, 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
            std::cerr << "Failed to submit draw command buffer." << std::endl;
            return false;
        }

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished_[current_frame_];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_;
        present_info.pImageIndices = &image_index;

        VkResult present = vkQueuePresentKHR(queue_, &present_info);
        if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
            if (!recreate_swapchain()) return false;
        } else if (present != VK_SUCCESS) {
            std::cerr << "Failed to present swapchain image." << std::endl;
            return false;
        }

        current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
        return true;
    }

    void shutdown() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }

        cleanup_swapchain();

        if (!command_buffers_.empty()) {
            vkFreeCommandBuffers(device_, command_pool_,
                                 static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
            command_buffers_.clear();
        }

        for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (render_finished_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, render_finished_[i], nullptr);
            }
            if (image_available_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, image_available_[i], nullptr);
            }
            if (in_flight_fences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            }
        }

        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
        }

        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        }

        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
        }

        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    VkRenderPass render_pass() const { return render_pass_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }
    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    uint32_t queue_family_index() const { return queue_family_index_; }
    VkQueue queue() const { return queue_; }
    uint32_t min_image_count() const { return min_image_count_; }
    VkExtent2D swapchain_extent() const { return swapchain_extent_; }

private:
    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr VkClearColorValue kClearColor = {{0.05f, 0.07f, 0.12f, 1.0f}};

    bool create_instance() {
        uint32_t extension_count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (!extensions || extension_count == 0) {
            std::cerr << "Failed to query Vulkan instance extensions: " << SDL_GetError() << std::endl;
            return false;
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Rayol Prototype";
        app_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.pEngineName = "Rayol";
        app_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = extension_count;
        create_info.ppEnabledExtensionNames = extensions;

        if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
            std::cerr << "vkCreateInstance failed." << std::endl;
            return false;
        }
        return true;
    }

    bool create_surface() {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
            std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << std::endl;
            return false;
        }
        return true;
    }

    bool pick_physical_device() {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        if (device_count == 0) {
            std::cerr << "No Vulkan physical devices found." << std::endl;
            return false;
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

        for (const auto& device : devices) {
            if (is_device_suitable(device)) {
                physical_device_ = device;
                return true;
            }
        }

        std::cerr << "No suitable Vulkan device found." << std::endl;
        return false;
    }

    bool is_device_suitable(VkPhysicalDevice device) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        if (queue_family_count == 0) {
            return false;
        }

        std::vector<VkQueueFamilyProperties> families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

        for (uint32_t i = 0; i < queue_family_count; ++i) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                queue_family_index_ = i;
                return true;
            }
        }
        return false;
    }

    bool create_device() {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family_index_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.enabledExtensionCount = 1;
        create_info.ppEnabledExtensionNames = device_extensions;

        if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
            std::cerr << "Failed to create logical device." << std::endl;
            return false;
        }

        vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
        return true;
    }

    bool create_descriptor_pool() {
        // Pool sizes from ImGui example, enough for basic UI.
        const std::array<VkDescriptorPoolSize, 11> pool_sizes = {{
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        }};

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
            std::cerr << "Failed to create descriptor pool." << std::endl;
            return false;
        }
        return true;
    }

    bool create_swapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
        if (format_count == 0) {
            std::cerr << "No surface formats available." << std::endl;
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
        if (present_mode_count == 0) {
            std::cerr << "No present modes available." << std::endl;
            return false;
        }
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());

        VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
        VkPresentModeKHR present_mode = choose_present_mode(present_modes);
        VkExtent2D extent = choose_extent(capabilities);

        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
            std::cerr << "Failed to create swapchain." << std::endl;
            return false;
        }

        vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
        swapchain_images_.resize(image_count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

        swapchain_format_ = surface_format.format;
        swapchain_extent_ = extent;
        min_image_count_ = image_count;

        images_in_flight_.assign(image_count, VK_NULL_HANDLE);
        return true;
    }

    bool create_image_views() {
        swapchain_image_views_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); ++i) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = swapchain_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format_;
            view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]) != VK_SUCCESS) {
                std::cerr << "Failed to create image views." << std::endl;
                return false;
            }
        }
        return true;
    }

    bool create_render_pass() {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = swapchain_format_;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        if (vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
            std::cerr << "Failed to create render pass." << std::endl;
            return false;
        }
        return true;
    }

    bool create_framebuffers() {
        framebuffers_.resize(swapchain_image_views_.size());
        for (size_t i = 0; i < swapchain_image_views_.size(); ++i) {
            VkImageView attachments[] = {swapchain_image_views_[i]};

            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass_;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = swapchain_extent_.width;
            framebuffer_info.height = swapchain_extent_.height;
            framebuffer_info.layers = 1;

            if (vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
                std::cerr << "Failed to create framebuffer." << std::endl;
                return false;
            }
        }
        return true;
    }

    bool create_command_pool() {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_index_;

        if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
            std::cerr << "Failed to create command pool." << std::endl;
            return false;
        }
        return true;
    }

    bool allocate_command_buffers() {
        command_buffers_.resize(framebuffers_.size());

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());

        if (vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
            std::cerr << "Failed to allocate command buffers." << std::endl;
            return false;
        }
        return true;
    }

    bool create_sync_objects() {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_[i]) != VK_SUCCESS ||
                vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
                std::cerr << "Failed to create synchronization objects." << std::endl;
                return false;
            }
        }
        return true;
    }

    void cleanup_swapchain() {
        for (auto framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device_, framebuffer, nullptr);
            }
        }
        framebuffers_.clear();

        for (auto view : swapchain_image_views_) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, view, nullptr);
            }
        }
        swapchain_image_views_.clear();

        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    bool recreate_swapchain() {
        vkDeviceWaitIdle(device_);

        cleanup_swapchain();

        if (!command_buffers_.empty()) {
            vkFreeCommandBuffers(device_, command_pool_,
                                 static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
            command_buffers_.clear();
        }

        if (!create_swapchain()) return false;
        if (!create_image_views()) return false;
        if (!create_render_pass()) return false;
        if (!create_framebuffers()) return false;
        if (!allocate_command_buffers()) return false;

#if RAYOL_USE_IMGUI
        if (imgui_layer_) {
            imgui_layer_->on_swapchain_recreated(render_pass_, min_image_count_);
        }
#endif

        return true;
    }

    void record_commands(VkCommandBuffer cmd, size_t image_index) {
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkClearValue clear_value{};
        clear_value.color = kClearColor;

        VkRenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass_;
        render_pass_info.framebuffer = framebuffers_[image_index];
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = swapchain_extent_;
        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

#if RAYOL_USE_IMGUI
        if (imgui_layer_) {
            imgui_layer_->end_frame(cmd, swapchain_extent_);
        }
#endif

        vkCmdEndRenderPass(cmd);

        vkEndCommandBuffer(cmd);
    }

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats[0];
    }

    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);

        VkExtent2D actual_extent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
        };

        actual_extent.width = std::max(capabilities.minImageExtent.width,
                                       std::min(capabilities.maxImageExtent.width, actual_extent.width));
        actual_extent.height = std::max(capabilities.minImageExtent.height,
                                        std::min(capabilities.maxImageExtent.height, actual_extent.height));
        return actual_extent;
    }

    SDL_Window* window_{nullptr};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    uint32_t queue_family_index_{0};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};

    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat swapchain_format_{};
    VkExtent2D swapchain_extent_{};
    uint32_t min_image_count_{0};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;

    VkRenderPass render_pass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool command_pool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers_;

    std::vector<VkFence> images_in_flight_;
    std::array<VkSemaphore, kMaxFramesInFlight> image_available_{};
    std::array<VkSemaphore, kMaxFramesInFlight> render_finished_{};
    std::array<VkFence, kMaxFramesInFlight> in_flight_fences_{};

    size_t current_frame_{0};
    ImGuiLayer* imgui_layer_{nullptr};
};

#endif  // RAYOL_ENABLE_VULKAN

class App {
public:
    int run() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
            return 1;
        }

        const Uint32 windowFlags = SDL_WINDOW_RESIZABLE |
#if RAYOL_ENABLE_VULKAN
                                   SDL_WINDOW_VULKAN |
#endif
                                   0;

        SDL_Window* window = SDL_CreateWindow("Rayol Prototype", 960, 540, windowFlags);
        if (!window) {
            std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

#if RAYOL_ENABLE_VULKAN
        if (!vk_.init(window)) {
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

#if RAYOL_USE_IMGUI
        ImGuiLayer::InitInfo imgui_info{};
        imgui_info.window = window;
        imgui_info.instance = vk_.instance();
        imgui_info.physical_device = vk_.physical_device();
        imgui_info.device = vk_.device();
        imgui_info.queue_family = vk_.queue_family_index();
        imgui_info.queue = vk_.queue();
        imgui_info.descriptor_pool = vk_.descriptor_pool();
        imgui_info.min_image_count = vk_.min_image_count();
        imgui_info.render_pass = vk_.render_pass();

        if (!imgui_layer_.init(imgui_info)) {
            vk_.shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        vk_.set_imgui_layer(&imgui_layer_);
#endif
#endif

        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
#if RAYOL_USE_IMGUI
                imgui_layer_.process_event(event);
#endif
            }

#if RAYOL_ENABLE_VULKAN
            bool ui_requested_exit = false;
            if (!vk_.draw_frame(ui_requested_exit)) {
                running = false;
            }
            if (ui_requested_exit) {
                running = false;
            }
#else
            SDL_Delay(16);
#endif
        }

#if RAYOL_USE_IMGUI
        imgui_layer_.shutdown();
#endif
#if RAYOL_ENABLE_VULKAN
        vk_.shutdown();
#endif
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

private:
#if RAYOL_ENABLE_VULKAN
    VulkanContext vk_{};
#if RAYOL_USE_IMGUI
    ImGuiLayer imgui_layer_{};
#endif
#endif
};

}  // namespace rayol

int main() {
    rayol::App app;
    return app.run();
}
