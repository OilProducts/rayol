#include "vulkan/frame_sync.h"

#include <iostream>

namespace rayol {

FrameSync::~FrameSync() = default;

// Create per-frame semaphores and fences.
bool FrameSync::init(VkDevice device) {
    if (initialized_) return true;

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < max_frames_; ++i) {
        if (vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create synchronization objects." << std::endl;
            return false;
        }
    }
    initialized_ = true;
    return true;
}

// Destroy sync objects.
void FrameSync::cleanup(VkDevice device) {
    if (!initialized_) return;
    for (uint32_t i = 0; i < max_frames_; ++i) {
        if (render_finished_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, render_finished_[i], nullptr);
            render_finished_[i] = VK_NULL_HANDLE;
        }
        if (image_available_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, image_available_[i], nullptr);
            image_available_[i] = VK_NULL_HANDLE;
        }
        if (in_flight_fences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, in_flight_fences_[i], nullptr);
            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }
    images_in_flight_.clear();
    initialized_ = false;
}

// Acquire swapchain image and sync fences for the current frame.
bool FrameSync::acquire(VkDevice device, VkSwapchainKHR swapchain, uint32_t& image_index) {
    vkWaitForFences(device, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                             image_available_[current_frame_], VK_NULL_HANDLE,
                                             &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        std::cerr << "Failed to acquire swapchain image." << std::endl;
        return false;
    }

    if (image_index >= images_in_flight_.size()) {
        images_in_flight_.resize(image_index + 1, VK_NULL_HANDLE);
    }

    if (images_in_flight_[image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &images_in_flight_[image_index], VK_TRUE, UINT64_MAX);
    }
    images_in_flight_[image_index] = in_flight_fences_[current_frame_];

    vkResetFences(device, 1, &in_flight_fences_[current_frame_]);
    return true;
}

// Submit a single command buffer with the current frame semaphores/fence.
bool FrameSync::submit(VkQueue queue, VkCommandBuffer cmd, VkFence fence, VkSemaphore wait_sem, VkSemaphore signal_sem) {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &wait_sem;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &signal_sem;

    if (vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
        std::cerr << "Failed to submit draw command buffer." << std::endl;
        return false;
    }
    return true;
}

// Present the acquired image; returns false if swapchain is out of date.
bool FrameSync::present(VkQueue queue, VkSwapchainKHR swapchain, uint32_t image_index, VkSemaphore wait_sem) {
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &wait_sem;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    VkResult present = vkQueuePresentKHR(queue, &present_info);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (present != VK_SUCCESS) {
        std::cerr << "Failed to present swapchain image." << std::endl;
        return false;
    }
    return true;
}

void FrameSync::track_image_fence(uint32_t image_index, VkFence fence) {
    if (image_index >= images_in_flight_.size()) {
        images_in_flight_.resize(image_index + 1, VK_NULL_HANDLE);
    }
    images_in_flight_[image_index] = fence;
}

VkFence FrameSync::image_fence(uint32_t image_index) const {
    if (image_index >= images_in_flight_.size()) return VK_NULL_HANDLE;
    return images_in_flight_[image_index];
}

void FrameSync::reset_image_fence(uint32_t image_index) {
    if (image_index < images_in_flight_.size()) {
        images_in_flight_[image_index] = VK_NULL_HANDLE;
    }
}

}  // namespace rayol
