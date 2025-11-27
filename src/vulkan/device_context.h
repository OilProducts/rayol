#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace rayol {

class DeviceContext {
public:
    ~DeviceContext();

    bool init(SDL_Window* window);

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queue_family_index() const { return queue_family_index_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkDescriptorPool descriptor_pool() const { return descriptor_pool_; }

private:
    bool create_instance();
    bool create_surface(SDL_Window* window);
    bool pick_physical_device();
    bool is_device_suitable(VkPhysicalDevice device);
    bool create_device();
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
