#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace rayol {

class FrameSync {
public:
    explicit FrameSync(uint32_t max_frames = 2) : max_frames_(max_frames) {}
    ~FrameSync();

    // Create per-frame semaphores and fences.
    bool init(VkDevice device);
    // Destroy all sync objects.
    void cleanup(VkDevice device);

    // Acquire the next swapchain image; returns false on out-of-date/suboptimal.
    bool acquire(VkDevice device, VkSwapchainKHR swapchain, uint32_t& image_index);
    // Submit a single command buffer with wait/signal semaphores.
    bool submit(VkQueue queue, VkCommandBuffer cmd, VkFence fence, VkSemaphore wait_sem, VkSemaphore signal_sem);
    // Present the current image; returns false on out-of-date/suboptimal.
    bool present(VkQueue queue, VkSwapchainKHR swapchain, uint32_t image_index, VkSemaphore wait_sem);

    VkFence current_in_flight_fence() const { return in_flight_fences_[current_frame_]; }
    VkSemaphore current_image_available() const { return image_available_[current_frame_]; }
    VkSemaphore current_render_finished() const { return render_finished_[current_frame_]; }
    uint32_t current_frame() const { return current_frame_; }
    void advance_frame() { current_frame_ = (current_frame_ + 1) % max_frames_; }

    void track_image_fence(uint32_t image_index, VkFence fence);
    VkFence image_fence(uint32_t image_index) const;
    void reset_image_fence(uint32_t image_index);

private:
    bool initialized_{false};
    uint32_t max_frames_{2};
    uint32_t current_frame_{0};
    std::array<VkSemaphore, 3> image_available_{};
    std::array<VkSemaphore, 3> render_finished_{};
    std::array<VkFence, 3> in_flight_fences_{};
    std::vector<VkFence> images_in_flight_;
};

}  // namespace rayol
