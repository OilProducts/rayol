#pragma once

#include <vulkan/vulkan.h>

#include <vector>
#include <iostream>

#include "fluid_experiment.h"

namespace rayol::fluid {

// GPU bridge for the fluid experiment: uploads particles, runs compute splat, and ray marches the density.
class FluidRenderer {
public:
    FluidRenderer() = default;

    struct CameraData {
        fluid::Vec3 pos{0.0f, 0.0f, -1.0f};
        fluid::Vec3 forward{0.0f, 0.0f, 1.0f};
        fluid::Vec3 right{1.0f, 0.0f, 0.0f};
        float tan_half_fov{0.577f};  // tan(30 deg)
        float aspect{16.0f / 9.0f};
    };

    bool init(VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, VkQueue queue,
              VkDescriptorPool descriptor_pool, VkRenderPass render_pass, VkExtent2D swapchain_extent,
              bool atomic_float_supported);
    void on_swapchain_recreated(VkRenderPass render_pass, VkExtent2D swapchain_extent);
    void cleanup();

    // Record compute work (before render pass) and graphics work (inside render pass).
    void record_compute(VkCommandBuffer cmd, const FluidExperiment& sim, bool enabled);
    void set_camera(const CameraData& cam) { fluid_draw_camera_ = cam; }

    void record_draw(VkCommandBuffer cmd, const FluidExperiment& sim, bool enabled, uint32_t frame_index,
                     float density_scale, float absorption);

private:
    struct Buffer {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{0};
    };

    struct Image {
        VkImage handle{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent3D extent{};
    };

    bool init_pipelines();
    bool create_compute_pipeline();
    bool create_graphics_pipeline();
    void destroy_pipelines();

    bool ensure_particle_buffer(size_t count);
    bool ensure_density_image(const VolumeConfig& cfg);
    bool ensure_noise_image();
    bool update_descriptors();

    bool write_particles(const std::vector<Particle>& particles);
    bool ensure_cpu_staging(size_t byte_size);
    void upload_cpu_density(VkCommandBuffer cmd, const FluidExperiment& sim);

    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const;
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, Buffer& out);
    void destroy_buffer(Buffer& buf);
    bool create_image(VkImageType type, VkImageViewType view_type, VkExtent3D extent, VkFormat format,
                      VkImageUsageFlags usage, VkMemoryPropertyFlags flags, Image& out);
    void destroy_image(Image& img);
    bool create_sampler(VkFilter filter, VkSampler& sampler);

    bool load_shader(const char* path, VkShaderModule& out_module);

    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                          VkImageAspectFlags aspect);
    void barrier_compute_to_fragment(VkCommandBuffer cmd, VkImage image);

    void log_once(const char* msg, bool& flag) {
        if (!flag) {
            std::cerr << msg << std::endl;
            flag = true;
        }
    }

    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t queue_family_{0};
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    VkExtent2D swapchain_extent_{};
    bool atomic_float_supported_{false};
    bool warned_no_compute_{false};
    bool warned_no_pipeline_{false};
    bool warned_no_density_{false};
    bool warned_descriptor_{false};
    bool logged_compute_start_{false};
    bool logged_draw_start_{false};

    VkDescriptorSetLayout compute_set_layout_{VK_NULL_HANDLE};
    VkPipelineLayout compute_pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline compute_pipeline_{VK_NULL_HANDLE};
    VkDescriptorSet compute_set_{VK_NULL_HANDLE};

    VkDescriptorSetLayout graphics_set_layout_{VK_NULL_HANDLE};
    VkPipelineLayout graphics_pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline graphics_pipeline_{VK_NULL_HANDLE};
    VkDescriptorSet graphics_set_{VK_NULL_HANDLE};

    Buffer particle_buffer_{};
    Buffer cpu_staging_{};  // Host-visible staging for CPU density upload (debug fallback).
    Image density_image_{};
    VkSampler density_sampler_{VK_NULL_HANDLE};
    VkImageLayout density_layout_{VK_IMAGE_LAYOUT_UNDEFINED};

    Image noise_image_{};
    VkSampler noise_sampler_{VK_NULL_HANDLE};
    VkImageLayout noise_layout_{VK_IMAGE_LAYOUT_UNDEFINED};

    CameraData fluid_draw_camera_{};
};

}  // namespace rayol::fluid
