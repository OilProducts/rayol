#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <vector>

#include "vulkan/device_context.h"
#include "ui/imgui_layer.h"
#include "vulkan/command_pool.h"
#include "vulkan/frame_sync.h"
#include "vulkan/swapchain.h"

namespace rayol {

// Owns Vulkan instance/swapchain/sync and records a simple clear (and optional ImGui) each frame.
class VulkanContext {
public:
    bool init(SDL_Window* window);
    void set_imgui_layer(ImGuiLayer* layer) { imgui_layer_ = layer; }
    bool draw_frame(bool& should_close_ui, const std::function<void(bool&)>& ui_callback);
    void shutdown();

    VkRenderPass render_pass() const { return swapchain_.render_pass(); }
    VkDescriptorPool descriptor_pool() const { return device_.descriptor_pool(); }
    VkInstance instance() const { return device_.instance(); }
    VkPhysicalDevice physical_device() const { return device_.physical_device(); }
    VkDevice device() const { return device_.device(); }
    uint32_t queue_family_index() const { return device_.queue_family_index(); }
    VkQueue queue() const { return device_.queue(); }
    uint32_t min_image_count() const { return swapchain_.min_image_count(); }
    VkExtent2D swapchain_extent() const { return swapchain_.extent(); }

private:
    static constexpr VkClearColorValue kClearColor = {{0.05f, 0.07f, 0.12f, 1.0f}};
    void record_commands(VkCommandBuffer cmd, size_t image_index);

    SDL_Window* window_{nullptr};
    DeviceContext device_{};
    Swapchain swapchain_{};
    CommandPool command_pool_{};
    FrameSync sync_{};

    ImGuiLayer* imgui_layer_{nullptr};
};

}  // namespace rayol
