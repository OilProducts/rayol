#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace rayol {

class CommandPool {
public:
    ~CommandPool();

    bool init(VkDevice device, uint32_t queue_family);
    void cleanup(VkDevice device);

    bool allocate(VkDevice device, uint32_t count);
    const std::vector<VkCommandBuffer>& buffers() const { return buffers_; }

private:
    VkCommandPool pool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> buffers_;
};

}  // namespace rayol
