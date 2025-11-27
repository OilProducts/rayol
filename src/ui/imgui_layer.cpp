#include "imgui_layer.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <vector>

namespace rayol {

bool ImGuiLayer::init(const InitInfo& init_info) {
    info_ = init_info;

    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(info_.window);

    ImGui_ImplVulkan_InitInfo vk_info{};
    vk_info.Instance = info_.instance;
    vk_info.PhysicalDevice = info_.physical_device;
    vk_info.Device = info_.device;
    vk_info.QueueFamily = info_.queue_family;
    vk_info.Queue = info_.queue;
    vk_info.DescriptorPool = info_.descriptor_pool;
    vk_info.RenderPass = info_.render_pass;
    vk_info.MinImageCount = info_.min_image_count;
    vk_info.ImageCount = info_.min_image_count;
    vk_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vk_info.UseDynamicRendering = VK_FALSE;

    if (!ImGui_ImplVulkan_Init(&vk_info)) {
        return false;
    }

    if (!upload_fonts()) {
        return false;
    }

    return true;
}

bool ImGuiLayer::upload_fonts() {
    // Create fonts texture using a one-off command buffer.
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = info_.queue_family;

    VkCommandPool font_pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(info_.device, &pool_info, nullptr, &font_pool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = font_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(info_.device, &alloc_info, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(info_.device, font_pool, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        vkFreeCommandBuffers(info_.device, font_pool, 1, &cmd);
        vkDestroyCommandPool(info_.device, font_pool, nullptr);
        return false;
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(info_.queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(info_.queue);

    vkFreeCommandBuffers(info_.device, font_pool, 1, &cmd);
    vkDestroyCommandPool(info_.device, font_pool, nullptr);
    return true;
}

void ImGuiLayer::begin_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::process_event(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::end_frame(VkCommandBuffer cmd, const VkExtent2D& extent) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiLayer::on_swapchain_recreated(VkRenderPass new_render_pass, uint32_t min_image_count) {
    info_.render_pass = new_render_pass;
    info_.min_image_count = min_image_count;
    ImGui_ImplVulkan_InitInfo vk_info{};
    vk_info.Instance = info_.instance;
    vk_info.PhysicalDevice = info_.physical_device;
    vk_info.Device = info_.device;
    vk_info.QueueFamily = info_.queue_family;
    vk_info.Queue = info_.queue;
    vk_info.DescriptorPool = info_.descriptor_pool;
    vk_info.MinImageCount = info_.min_image_count;
    vk_info.ImageCount = info_.min_image_count;
    vk_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vk_info.UseDynamicRendering = VK_FALSE;
    vk_info.RenderPass = info_.render_pass;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplVulkan_Init(&vk_info);
    upload_fonts();
}

void ImGuiLayer::shutdown() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    if (context_) {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
}

}  // namespace rayol
