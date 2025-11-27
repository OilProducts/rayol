#include "vulkan/device_context.h"

#include <iostream>
#include <vector>

namespace rayol {

// Destroy descriptor pool, device, surface, and instance.
DeviceContext::~DeviceContext() {
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

// Create instance/surface/device/queue/descriptor pool.
bool DeviceContext::init(SDL_Window* window) {
    if (!create_instance()) return false;
    if (!create_surface(window)) return false;
    if (!pick_physical_device()) return false;
    if (!create_device()) return false;
    if (!create_descriptor_pool()) return false;
    return true;
}

// Build a Vulkan instance with SDL-requested extensions.
bool DeviceContext::create_instance() {
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

// Create a presentation surface from the SDL window.
bool DeviceContext::create_surface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

// Pick a GPU that supports graphics + present on our surface.
bool DeviceContext::pick_physical_device() {
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

// Check if a GPU has a graphics+present queue family.
bool DeviceContext::is_device_suitable(VkPhysicalDevice device) {
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

// Create logical device and fetch graphics/present queue.
bool DeviceContext::create_device() {
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

// Allocate a descriptor pool for ImGui/resources.
bool DeviceContext::create_descriptor_pool() {
    const VkDescriptorPoolSize pool_sizes[] = {
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
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool." << std::endl;
        return false;
    }
    return true;
}

}  // namespace rayol
