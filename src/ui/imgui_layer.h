#pragma once

#include <SDL3/SDL.h>

#if RAYOL_USE_IMGUI
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>

struct ImGuiContext;

namespace rayol {

// Minimal ImGui wrapper to keep backend glue out of the main loop.
class ImGuiLayer {
public:
    struct InitInfo {
        SDL_Window* window{};
        VkInstance instance{};
        VkPhysicalDevice physical_device{};
        VkDevice device{};
        uint32_t queue_family{};
        VkQueue queue{};
        VkDescriptorPool descriptor_pool{};
        uint32_t min_image_count{};
        VkRenderPass render_pass{};
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

#else

// No-op stub when ImGui is disabled.
namespace rayol {
class ImGuiLayer {
public:
    struct InitInfo {
        SDL_Window* window{};
        VkInstance instance{};
        VkPhysicalDevice physical_device{};
        VkDevice device{};
        uint32_t queue_family{};
        VkQueue queue{};
        VkDescriptorPool descriptor_pool{};
        uint32_t min_image_count{};
        VkRenderPass render_pass{};
    };

    bool init(const InitInfo&) { return true; }
    void process_event(const SDL_Event&) {}
    void begin_frame() {}
    void end_frame(VkCommandBuffer, const VkExtent2D&) {}
    void shutdown() {}
    void on_swapchain_recreated(VkRenderPass, uint32_t) {}
};
}  // namespace rayol

#endif
