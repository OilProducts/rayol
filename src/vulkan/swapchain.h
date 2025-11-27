#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <vector>

#include "vulkan/device_context.h"

namespace rayol {

class Swapchain {
public:
    ~Swapchain();

    bool init(DeviceContext& device, SDL_Window* window);
    bool recreate(DeviceContext& device, SDL_Window* window);
    void cleanup(DeviceContext& device);

    VkRenderPass render_pass() const { return render_pass_; }
    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    VkSwapchainKHR handle() const { return swapchain_; }
    uint32_t min_image_count() const { return static_cast<uint32_t>(images_.size()); }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }

private:
    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D choose_extent(SDL_Window* window, const VkSurfaceCapabilitiesKHR& capabilities);

    bool create_swapchain(DeviceContext& device, SDL_Window* window);
    bool create_image_views(DeviceContext& device);
    bool create_render_pass(DeviceContext& device);
    bool create_framebuffers(DeviceContext& device);

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat format_{};
    VkExtent2D extent_{};

    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
};

}  // namespace rayol
