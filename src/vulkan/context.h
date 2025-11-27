#pragma once

#include <SDL3/SDL.h>
#if RAYOL_ENABLE_VULKAN
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "ui/imgui_layer.h"

namespace rayol {

// Owns Vulkan instance/swapchain/sync and records a simple clear (and optional ImGui) each frame.
class VulkanContext {
public:
    bool init(SDL_Window* window);
    void set_imgui_layer(ImGuiLayer* layer) { imgui_layer_ = layer; }
    bool draw_frame(bool& should_close_ui);
    void shutdown();

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

    bool create_instance();
    bool create_surface();
    bool pick_physical_device();
    bool is_device_suitable(VkPhysicalDevice device);
    bool create_device();
    bool create_descriptor_pool();
    bool create_swapchain();
    bool create_image_views();
    bool create_render_pass();
    bool create_framebuffers();
    bool create_command_pool();
    bool allocate_command_buffers();
    bool create_sync_objects();

    void cleanup_swapchain();
    bool recreate_swapchain();
    void record_commands(VkCommandBuffer cmd, size_t image_index);
    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities);

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

}  // namespace rayol

#endif  // RAYOL_ENABLE_VULKAN
