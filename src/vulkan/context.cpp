#include "vulkan/context.h"

#include <imgui.h>

#include <iostream>

namespace rayol {

// Initialize device, swapchain, command buffers, and sync objects.
bool VulkanContext::init(SDL_Window* window) {
    window_ = window;
    if (!device_.init(window_)) return false;
    if (!swapchain_.init(device_, window_)) return false;
    if (!command_pool_.init(device_.device(), device_.queue_family_index())) return false;
    if (!command_pool_.allocate(device_.device(), static_cast<uint32_t>(swapchain_.framebuffers().size()))) return false;
    if (!sync_.init(device_.device())) return false;
    return true;
}

// Draw a frame with clear + optional UI; handles swapchain recreation on resize.
bool VulkanContext::draw_frame(bool& should_close_ui, const std::function<void(bool&)>& ui_callback,
                               const FluidDrawData* fluid) {
    uint32_t image_index = 0;
    if (!sync_.acquire(device_.device(), swapchain_.handle(), image_index)) {
        if (!swapchain_.recreate(device_, window_)) return false;
        if (!command_pool_.allocate(device_.device(), static_cast<uint32_t>(swapchain_.framebuffers().size()))) return false;
        if (imgui_layer_) {
            imgui_layer_->on_swapchain_recreated(swapchain_.render_pass(), swapchain_.min_image_count());
        }
        if (fluid && fluid->renderer) {
            fluid->renderer->on_swapchain_recreated(swapchain_.render_pass(), swapchain_.extent());
        }
        return true;
    }

    if (imgui_layer_) {
        imgui_layer_->begin_frame();
        if (ui_callback) {
            ui_callback(should_close_ui);
        }
    }

    VkCommandBuffer cmd = command_pool_.buffers()[image_index];
    vkResetCommandBuffer(cmd, 0);
    record_commands(cmd, image_index, fluid);

    if (!sync_.submit(device_.queue(), cmd, sync_.current_in_flight_fence(),
                      sync_.current_image_available(), sync_.current_render_finished())) {
        return false;
    }

    if (!sync_.present(device_.queue(), swapchain_.handle(), image_index, sync_.current_render_finished())) {
        if (!swapchain_.recreate(device_, window_)) return false;
        if (!command_pool_.allocate(device_.device(), static_cast<uint32_t>(swapchain_.framebuffers().size()))) return false;
        if (imgui_layer_) {
            imgui_layer_->on_swapchain_recreated(swapchain_.render_pass(), swapchain_.min_image_count());
        }
        if (fluid && fluid->renderer) {
            fluid->renderer->on_swapchain_recreated(swapchain_.render_pass(), swapchain_.extent());
        }
    }

    sync_.advance_frame();
    return true;
}

// Wait for idle and release all Vulkan resources.
void VulkanContext::shutdown() {
    if (device_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_.device());
    }

    sync_.cleanup(device_.device());
    command_pool_.cleanup(device_.device());
    swapchain_.cleanup(device_);
}

// Record a render pass that clears the target and draws ImGui, if present.
void VulkanContext::record_commands(VkCommandBuffer cmd, size_t image_index, const FluidDrawData* fluid) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Fluid compute before the render pass.
    if (fluid && fluid->renderer && fluid->sim) {
        fluid->renderer->record_compute(cmd, *fluid->sim, fluid->enabled);
    }

    VkClearValue clear_value{};
    clear_value.color = kClearColor;

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = swapchain_.render_pass();
    render_pass_info.framebuffer = swapchain_.framebuffers()[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = swapchain_.extent();
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    if (fluid && fluid->renderer && fluid->sim) {
        fluid->renderer->record_draw(cmd, *fluid->sim, fluid->enabled, fluid->frame_index,
                                     fluid->density_scale, fluid->absorption);
    }
    if (imgui_layer_) {
        imgui_layer_->end_frame(cmd, swapchain_.extent());
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

}  // namespace rayol
