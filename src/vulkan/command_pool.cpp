#include "vulkan/command_pool.h"

#include <iostream>

namespace rayol {

CommandPool::~CommandPool() = default;

// Create a resettable command pool for the given queue family.
bool CommandPool::init(VkDevice device, uint32_t queue_family) {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;

    if (vkCreateCommandPool(device, &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool." << std::endl;
        return false;
    }
    return true;
}

// Free command buffers and destroy pool.
void CommandPool::cleanup(VkDevice device) {
    if (!buffers_.empty()) {
        vkFreeCommandBuffers(device, pool_, static_cast<uint32_t>(buffers_.size()), buffers_.data());
        buffers_.clear();
    }
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

// Allocate primary command buffers from the pool.
bool CommandPool::allocate(VkDevice device, uint32_t count) {
    if (pool_ == VK_NULL_HANDLE) return false;
    buffers_.resize(count);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device, &alloc_info, buffers_.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers." << std::endl;
        buffers_.clear();
        return false;
    }
    return true;
}

}  // namespace rayol
