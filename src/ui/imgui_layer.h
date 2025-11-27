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

    bool init(const InitInfo& info);
    void process_event(const SDL_Event& event);
    void begin_frame();
    void end_frame(VkCommandBuffer cmd, const VkExtent2D& extent);
    void shutdown();

    void on_swapchain_recreated(VkRenderPass new_render_pass, uint32_t min_image_count);

private:
    bool upload_fonts();

    ImGuiContext* context_{nullptr};
    InitInfo info_{};
};

}  // namespace rayol
