#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace rayol {

class CommandPool {
public:
    ~CommandPool();

    // Create a command pool for a given queue family.
    bool init(VkDevice device, uint32_t queue_family);
    // Free buffers and destroy the command pool.
    void cleanup(VkDevice device);

    // Allocate primary command buffers from the pool.
    bool allocate(VkDevice device, uint32_t count);
    const std::vector<VkCommandBuffer>& buffers() const { return buffers_; }

private:
    VkCommandPool pool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> buffers_;
};

}  // namespace rayol
