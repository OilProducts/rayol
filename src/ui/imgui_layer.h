#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

struct ImGuiContext;

namespace rayol {

// Minimal ImGui wrapper to keep backend glue out of the main loop.
class ImGuiLayer {
public:
    struct InitInfo {
        SDL_Window* window{};              // SDL window handle used by the backend
        VkInstance instance{};             // Vulkan instance
        VkPhysicalDevice physical_device{};  // Physical device for capabilities
        VkDevice device{};                 // Logical device
        uint32_t queue_family{};           // Graphics/Present queue family index
        VkQueue queue{};                   // Graphics/Present queue
        VkDescriptorPool descriptor_pool{};  // Descriptor pool for ImGui resources
        uint32_t min_image_count{};        // Swapchain image count
        VkRenderPass render_pass{};        // Render pass compatible with swapchain
    };

    // Initialize ImGui for SDL3 + Vulkan using the provided handles.
    bool init(const InitInfo& info);
    // Feed SDL events to ImGui (e.g., keyboard/mouse).
    void process_event(const SDL_Event& event);
    // Start a new ImGui frame.
    void begin_frame();
    // Render ImGui draw data into the provided command buffer.
    void end_frame(VkCommandBuffer cmd, const VkExtent2D& extent);
    // Shutdown ImGui and backend bindings.
    void shutdown();

    // Recreate backend resources after the swapchain/render pass changes.
    void on_swapchain_recreated(VkRenderPass new_render_pass, uint32_t min_image_count);

private:
    // Upload font textures using a transient command buffer.
    bool upload_fonts();

    ImGuiContext* context_{nullptr};
    InitInfo info_{};
};

}  // namespace rayol
