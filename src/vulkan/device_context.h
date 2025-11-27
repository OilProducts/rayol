#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace rayol {

class DeviceContext {
public:
    // Initialize instance, surface, physical/logical device, queue, and descriptor pool.
    ~DeviceContext();

    // Initialize instance, surface, physical/logical device, queue, and descriptor pool.
    bool init(SDL_Window* window);

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queue_family_index() const { return queue_family_index_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }

private:
    // Create Vulkan instance with required SDL extensions.
    bool create_instance();
    // Create presentation surface from the SDL window.
    bool create_surface(SDL_Window* window);
    // Select a suitable physical device/queue family for graphics + present.
    bool pick_physical_device();
    // Check if the physical device supports graphics + present.
    bool is_device_suitable(VkPhysicalDevice device);
    // Create logical device and graphics/present queue.
    bool create_device();
    // Allocate a descriptor pool for ImGui and future resources.
    bool create_descriptor_pool();

    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    uint32_t queue_family_index_{0};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
};

}  // namespace rayol
