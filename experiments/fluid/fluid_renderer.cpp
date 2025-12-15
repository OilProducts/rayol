#include "fluid_renderer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <string>

namespace rayol::fluid {

namespace {
#ifdef RAYOL_FLUID_SHADER_DIR
const char* kShaderDir = RAYOL_FLUID_SHADER_DIR "/";
#else
const char* kShaderDir = "../shaders/fluid/";
#endif
const char* kShaderDirFallback = "shaders/fluid/";
const char* kParticleSplatComp = "particle_splat.comp.spv";
const char* kVolumeRaymarchFrag = "volume_raymarch.frag.spv";
const char* kFullscreenVert = "fullscreen_uv.vert.spv";

struct ComputePush {
    float origin[3];
    float voxel_size;
    float kernel_radius;
    int dims[3];
    uint32_t particle_count;
};

struct GraphicsPush {
    float volume_origin[4];        // xyz origin, w = step
    float volume_extent[4];        // xyz extent, w = density scale
    float light_dir_absorb[4];     // xyz dir, w = absorption
    float light_color_ambient[4];  // xyz color, w = ambient
    float camera_pos[4];           // xyz position, w unused
    float camera_forward[4];       // xyz forward, w = tan(fov/2)
    float camera_right[4];         // xyz right, w = aspect
    float max_distance;
    uint32_t frame_index;
    uint32_t padding[2];
};

constexpr VkDeviceSize kParticleStride = sizeof(float) * 8;  // matches shader struct (vec4 + vec4)
}  // namespace

bool FluidRenderer::init(VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, VkQueue queue,
                         VkDescriptorPool descriptor_pool, VkRenderPass render_pass, VkExtent2D swapchain_extent,
                         bool atomic_float_supported) {
    physical_device_ = physical_device;
    device_ = device;
    queue_ = queue;
    queue_family_ = queue_family;
    descriptor_pool_ = descriptor_pool;
    render_pass_ = render_pass;
    swapchain_extent_ = swapchain_extent;
    atomic_float_supported_ = atomic_float_supported;

    std::cerr << "[fluid] init: atomic float supported = " << (atomic_float_supported_ ? "yes" : "no") << "\n";
    if (!ensure_noise_image()) {
        std::cerr << "[fluid] init: failed to create noise image.\n";
        return false;
    }
    if (!init_pipelines()) return false;
    return true;
}

void FluidRenderer::on_swapchain_recreated(VkRenderPass render_pass, VkExtent2D swapchain_extent) {
    render_pass_ = render_pass;
    swapchain_extent_ = swapchain_extent;
    destroy_pipelines();
    init_pipelines();
}

void FluidRenderer::cleanup() {
    destroy_pipelines();
    destroy_buffer(particle_buffer_);
    destroy_image(density_image_);
    density_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (density_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, density_sampler_, nullptr);
        density_sampler_ = VK_NULL_HANDLE;
    }
    destroy_image(noise_image_);
    noise_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (noise_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, noise_sampler_, nullptr);
        noise_sampler_ = VK_NULL_HANDLE;
    }
}

bool FluidRenderer::init_pipelines() {
    bool ok = create_compute_pipeline();
    if (!ok) {
        std::cerr << "[fluid] compute pipeline creation failed.\n";
    }
    bool gok = create_graphics_pipeline();
    if (!gok) {
        std::cerr << "[fluid] graphics pipeline creation failed.\n";
    }
    return ok && gok;
}

bool FluidRenderer::ensure_particle_buffer(size_t count) {
    VkDeviceSize needed = static_cast<VkDeviceSize>(count) * kParticleStride;
    if (particle_buffer_.handle != VK_NULL_HANDLE && needed <= particle_buffer_.size) {
        return true;
    }
    destroy_buffer(particle_buffer_);
    return create_buffer(needed, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, particle_buffer_);
}

bool FluidRenderer::ensure_density_image(const VolumeConfig& cfg) {
    VkExtent3D extent{
        static_cast<uint32_t>(cfg.dims.x),
        static_cast<uint32_t>(cfg.dims.y),
        static_cast<uint32_t>(cfg.dims.z),
    };
    if (density_image_.handle != VK_NULL_HANDLE &&
        density_image_.extent.width == extent.width &&
        density_image_.extent.height == extent.height &&
        density_image_.extent.depth == extent.depth) {
        if (density_image_.view == VK_NULL_HANDLE) {
            std::cerr << "[fluid] density image exists but view is null; recreating.\n";
            destroy_image(density_image_);
            density_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        } else {
            return true;
        }
    }
    destroy_image(density_image_);
    density_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (density_sampler_ == VK_NULL_HANDLE) {
        if (!create_sampler(VK_FILTER_LINEAR, density_sampler_)) return false;
    }
    bool ok = create_image(VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, extent, VK_FORMAT_R32_SFLOAT,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, density_image_);
    if (!ok) {
        std::cerr << "[fluid] failed to create density image.\n";
    } else {
        std::cerr << "[fluid] density image created: " << extent.width << "x" << extent.height << "x" << extent.depth << "\n";
    }
    return ok;
}

bool FluidRenderer::ensure_noise_image() {
    if (noise_image_.handle != VK_NULL_HANDLE) return true;
    static const float kNoise[16] = {
        0.12f, 0.73f, 0.34f, 0.91f,
        0.55f, 0.08f, 0.67f, 0.21f,
        0.42f, 0.95f, 0.14f, 0.63f,
        0.78f, 0.29f, 0.51f, 0.02f,
    };
    VkExtent3D extent{4, 4, 1};
    bool ok = create_image(VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, extent, VK_FORMAT_R32_SFLOAT,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, noise_image_);
    if (!ok) {
        std::cerr << "[fluid] failed to create noise image.\n";
        return false;
    }
    if (!create_sampler(VK_FILTER_NEAREST, noise_sampler_)) return false;
    noise_layout_ = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    Buffer staging_buf{};
    VkDeviceSize size = sizeof(kNoise);
    if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buf)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, staging_buf.memory, 0, size, 0, &mapped);
    std::memcpy(mapped, kNoise, size);
    vkUnmapMemory(device_, staging_buf.memory);

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = queue_family_;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        destroy_buffer(staging_buf);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &alloc_info, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(device_, pool, nullptr);
        destroy_buffer(staging_buf);
        return false;
    }

    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkBufferImageCopy copy{};
    copy.imageExtent = extent;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;

    transition_image(cmd, noise_image_.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdCopyBufferToImage(cmd, staging_buf.handle, noise_image_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition_image(cmd, noise_image_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    noise_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, pool, 1, &cmd);
    vkDestroyCommandPool(device_, pool, nullptr);
    destroy_buffer(staging_buf);
    return true;
}

bool FluidRenderer::write_particles(const std::vector<Particle>& particles) {
    if (particles.empty()) return true;
    if (!ensure_particle_buffer(particles.size())) return false;
    void* mapped = nullptr;
    vkMapMemory(device_, particle_buffer_.memory, 0, particle_buffer_.size, 0, &mapped);
    char* dst = static_cast<char*>(mapped);
    for (const auto& p : particles) {
        float data[8] = {p.position.x, p.position.y, p.position.z, p.radius,
                         p.velocity.x, p.velocity.y, p.velocity.z, p.mass};
        std::memcpy(dst, data, sizeof(data));
        dst += sizeof(data);
    }
    vkUnmapMemory(device_, particle_buffer_.memory);
    return true;
}

bool FluidRenderer::ensure_cpu_staging(size_t byte_size) {
    if (cpu_staging_.handle != VK_NULL_HANDLE && cpu_staging_.size >= byte_size) {
        return true;
    }
    destroy_buffer(cpu_staging_);
    return create_buffer(byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cpu_staging_);
}

void FluidRenderer::upload_cpu_density(VkCommandBuffer cmd, const FluidExperiment& sim) {
    const auto& density = sim.volume().density();
    if (density.empty()) return;

    size_t byte_size = density.size() * sizeof(float);
    if (!ensure_cpu_staging(byte_size)) {
        log_once("[fluid] Failed to create CPU staging buffer.", warned_no_density_);
        return;
    }

    void* mapped = nullptr;
    vkMapMemory(device_, cpu_staging_.memory, 0, byte_size, 0, &mapped);
    std::memcpy(mapped, density.data(), byte_size);
    vkUnmapMemory(device_, cpu_staging_.memory);

    // Transition to TRANSFER_DST, copy, then to SHADER_READ.
    transition_image(cmd, density_image_.handle, density_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    density_layout_ = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkBufferImageCopy copy{};
    copy.imageExtent = density_image_.extent;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    vkCmdCopyBufferToImage(cmd, cpu_staging_.handle, density_image_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy);

    transition_image(cmd, density_image_.handle, density_layout_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    density_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void FluidRenderer::record_compute(VkCommandBuffer cmd, const FluidExperiment& sim, bool enabled) {
    if (!enabled) return;
    log_once("[fluid] record_compute invoked.", logged_compute_start_);
    if (!ensure_density_image(sim.volume().config())) {
        log_once("[fluid] Failed to create/resize density image.", warned_no_density_);
        return;
    }
    // Debug spam reduced: layout info is still helpful once.
    log_once("[fluid] density image is ready for compute", logged_compute_start_);
    if (!update_descriptors()) {
        log_once("[fluid] Descriptor update failed; compute/draw skipped.", warned_descriptor_);
        return;
    }
    // Temporarily rely on CPU density upload for rendering while the GPU splat
    // path is being validated against the SPH CPU sim.
    bool gpu_splat = false;

    if (gpu_splat) {
        size_t particle_capacity = std::max<size_t>(1, sim.particles().size());
        if (!ensure_particle_buffer(particle_capacity)) return;
        write_particles(sim.particles());
    }

    if (gpu_splat) {
        log_once("[fluid] descriptors updated; dispatching compute", logged_draw_start_);

        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue zero{{0.0f, 0.0f, 0.0f, 0.0f}};
        transition_image(cmd, density_image_.handle, density_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        density_layout_ = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdClearColorImage(cmd, density_image_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        transition_image(cmd, density_image_.handle, density_layout_, VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        density_layout_ = VK_IMAGE_LAYOUT_GENERAL;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
        ComputePush push{};
        push.origin[0] = sim.volume().config().origin.x;
        push.origin[1] = sim.volume().config().origin.y;
        push.origin[2] = sim.volume().config().origin.z;
        push.voxel_size = sim.volume().config().voxel_size;
        push.kernel_radius = sim.settings().kernel_radius;
        push.dims[0] = sim.volume().config().dims.x;
        push.dims[1] = sim.volume().config().dims.y;
        push.dims[2] = sim.volume().config().dims.z;
        push.particle_count = static_cast<uint32_t>(sim.particles().size());
        vkCmdPushConstants(cmd, compute_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout_, 0, 1, &compute_set_, 0, nullptr);
        uint32_t groups = (push.particle_count + 127) / 128;
        if (groups > 0) {
            vkCmdDispatch(cmd, groups, 1, 1);
        } else {
            log_once("[fluid] No particles to dispatch; skipping compute.", warned_no_compute_);
        }

        barrier_compute_to_fragment(cmd, density_image_.handle);
        density_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else {
        // CPU fallback: upload density from the CPU volume when compute is unavailable.
        upload_cpu_density(cmd, sim);
    }
}

void FluidRenderer::record_draw(VkCommandBuffer cmd, const FluidExperiment& sim, bool enabled, uint32_t frame_index,
                                float density_scale, float absorption) {
    if (!enabled) return;
    log_once("[fluid] record_draw invoked.", logged_draw_start_);
    if (graphics_pipeline_ == VK_NULL_HANDLE) {
        log_once("[fluid] Graphics pipeline not created.", warned_no_pipeline_);
        return;
    }
    if (density_image_.view == VK_NULL_HANDLE) {
        log_once("[fluid] record_draw: density view missing.", warned_no_density_);
        return;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
    GraphicsPush gpush{};
    gpush.volume_origin[0] = sim.volume().config().origin.x;
    gpush.volume_origin[1] = sim.volume().config().origin.y;
    gpush.volume_origin[2] = sim.volume().config().origin.z;
    Vec3 ext = sim.volume_extent();
    gpush.volume_origin[3] = sim.volume().config().voxel_size * 0.75f;  // step
    gpush.volume_extent[0] = ext.x;
    gpush.volume_extent[1] = ext.y;
    gpush.volume_extent[2] = ext.z;
    gpush.volume_extent[3] = density_scale;  // density scale
    gpush.light_dir_absorb[0] = -0.4f;
    gpush.light_dir_absorb[1] = -1.0f;
    gpush.light_dir_absorb[2] = -0.2f;
    gpush.light_dir_absorb[3] = absorption;  // absorption
    gpush.light_color_ambient[0] = 1.0f;
    gpush.light_color_ambient[1] = 0.95f;
    gpush.light_color_ambient[2] = 0.9f;
    gpush.light_color_ambient[3] = 0.1f;  // ambient
    gpush.camera_pos[0] = fluid_draw_camera_.pos.x;
    gpush.camera_pos[1] = fluid_draw_camera_.pos.y;
    gpush.camera_pos[2] = fluid_draw_camera_.pos.z;
    gpush.camera_pos[3] = 0.0f;
    gpush.camera_forward[0] = fluid_draw_camera_.forward.x;
    gpush.camera_forward[1] = fluid_draw_camera_.forward.y;
    gpush.camera_forward[2] = fluid_draw_camera_.forward.z;
    gpush.camera_forward[3] = fluid_draw_camera_.tan_half_fov;
    gpush.camera_right[0] = fluid_draw_camera_.right.x;
    gpush.camera_right[1] = fluid_draw_camera_.right.y;
    gpush.camera_right[2] = fluid_draw_camera_.right.z;
    gpush.camera_right[3] = fluid_draw_camera_.aspect;
    gpush.max_distance = ext.z;
    gpush.frame_index = frame_index;
    vkCmdPushConstants(cmd, graphics_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpush), &gpush);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_layout_, 0, 1, &graphics_set_, 0,
                            nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool FluidRenderer::create_compute_pipeline() {
    VkShaderModule comp = VK_NULL_HANDLE;
    if (!load_shader(kParticleSplatComp, comp)) return false;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 2;
    set_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &compute_set_layout_) != VK_SUCCESS) {
        return false;
    }

    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = sizeof(ComputePush);
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &compute_set_layout_;
    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &compute_pipeline_layout_) != VK_SUCCESS) {
        return false;
    }

    VkComputePipelineCreateInfo pipe_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipe_info.layout = compute_pipeline_layout_;
    pipe_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                       comp, "main", nullptr};

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipe_info, nullptr, &compute_pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, comp, nullptr);
        return false;
    }
    vkDestroyShaderModule(device_, comp, nullptr);

    VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &compute_set_layout_;
    if (vkAllocateDescriptorSets(device_, &alloc_info, &compute_set_) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool FluidRenderer::create_graphics_pipeline() {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    if (!load_shader(kFullscreenVert, vert)) return false;
    if (!load_shader(kVolumeRaymarchFrag, frag)) {
        vkDestroyShaderModule(device_, vert, nullptr);
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 2;
    set_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &graphics_set_layout_) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(GraphicsPush);

    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &graphics_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &graphics_pipeline_layout_) != VK_SUCCESS) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = swapchain_extent_;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipe{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipe.stageCount = 2;
    pipe.pStages = stages;
    pipe.pVertexInputState = &vi;
    pipe.pInputAssemblyState = &ia;
    pipe.pViewportState = &vp;
    pipe.pRasterizationState = &rs;
    pipe.pMultisampleState = &ms;
    pipe.pDepthStencilState = &ds;
    pipe.pColorBlendState = &cb;
    pipe.layout = graphics_pipeline_layout_;
    pipe.renderPass = render_pass_;
    pipe.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipe, nullptr, &graphics_pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return false;
    }

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);

    VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &graphics_set_layout_;
    if (vkAllocateDescriptorSets(device_, &alloc_info, &graphics_set_) != VK_SUCCESS) {
        return false;
    }
    return true;
}

void FluidRenderer::destroy_pipelines() {
    if (compute_set_ != VK_NULL_HANDLE && descriptor_pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, descriptor_pool_, 1, &compute_set_);
        compute_set_ = VK_NULL_HANDLE;
    }
    if (compute_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, compute_pipeline_, nullptr);
        compute_pipeline_ = VK_NULL_HANDLE;
    }
    if (compute_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, compute_pipeline_layout_, nullptr);
        compute_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (compute_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, compute_set_layout_, nullptr);
        compute_set_layout_ = VK_NULL_HANDLE;
    }

    if (graphics_set_ != VK_NULL_HANDLE && descriptor_pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, descriptor_pool_, 1, &graphics_set_);
        graphics_set_ = VK_NULL_HANDLE;
    }
    if (graphics_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, graphics_pipeline_, nullptr);
        graphics_pipeline_ = VK_NULL_HANDLE;
    }
    if (graphics_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, graphics_pipeline_layout_, nullptr);
        graphics_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (graphics_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, graphics_set_layout_, nullptr);
        graphics_set_layout_ = VK_NULL_HANDLE;
    }
}

bool FluidRenderer::update_descriptors() {
    if (compute_set_ == VK_NULL_HANDLE || graphics_set_ == VK_NULL_HANDLE) {
        log_once("[fluid] Descriptor sets not allocated.", warned_descriptor_);
        return false;
    }
    if (density_image_.view == VK_NULL_HANDLE) {
        log_once("[fluid] Density image view missing.", warned_descriptor_);
        return false;
    }
    VkDescriptorBufferInfo buf{};
    buf.buffer = particle_buffer_.handle;
    buf.offset = 0;
    buf.range = particle_buffer_.size;

    VkDescriptorImageInfo density_storage{};
    density_storage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    density_storage.imageView = density_image_.view;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = compute_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &buf;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = compute_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &density_storage;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    VkDescriptorImageInfo density_sample{};
    density_sample.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    density_sample.imageView = density_image_.view;
    density_sample.sampler = density_sampler_;

    VkDescriptorImageInfo noise_sample{};
    noise_sample.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    noise_sample.imageView = noise_image_.view;
    noise_sample.sampler = noise_sampler_;

    VkWriteDescriptorSet gwrites[2]{};
    gwrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    gwrites[0].dstSet = graphics_set_;
    gwrites[0].dstBinding = 0;
    gwrites[0].descriptorCount = 1;
    gwrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    gwrites[0].pImageInfo = &density_sample;

    gwrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    gwrites[1].dstSet = graphics_set_;
    gwrites[1].dstBinding = 1;
    gwrites[1].descriptorCount = 1;
    gwrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    gwrites[1].pImageInfo = &noise_sample;
    vkUpdateDescriptorSets(device_, 2, gwrites, 0, nullptr);
    return true;
}

uint32_t FluidRenderer::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

bool FluidRenderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, Buffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &info, nullptr, &out.handle) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, out.handle, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, flags);
    if (vkAllocateMemory(device_, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, out.handle, nullptr);
        out.handle = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device_, out.handle, out.memory, 0);
    out.size = size;
    return true;
}

void FluidRenderer::destroy_buffer(Buffer& buf) {
    if (buf.handle != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buf.handle, nullptr);
        buf.handle = VK_NULL_HANDLE;
    }
    if (buf.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, buf.memory, nullptr);
        buf.memory = VK_NULL_HANDLE;
    }
    buf.size = 0;
}

bool FluidRenderer::create_image(VkImageType type, VkImageViewType view_type, VkExtent3D extent, VkFormat format,
                                 VkImageUsageFlags usage, VkMemoryPropertyFlags flags, Image& out) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = type;
    info.format = format;
    info.extent = extent;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device_, &info, nullptr, &out.handle) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, out.handle, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, flags);
    if (vkAllocateMemory(device_, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(device_, out.handle, nullptr);
        out.handle = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(device_, out.handle, out.memory, 0);
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = out.handle;
    view_info.viewType = view_type;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, &out.view) != VK_SUCCESS) {
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.handle, nullptr);
        out.handle = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        return false;
    }
    out.format = format;
    out.extent = extent;
    return true;
}

void FluidRenderer::destroy_image(Image& img) {
    if (img.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, img.view, nullptr);
        img.view = VK_NULL_HANDLE;
    }
    if (img.handle != VK_NULL_HANDLE) {
        vkDestroyImage(device_, img.handle, nullptr);
        img.handle = VK_NULL_HANDLE;
    }
    if (img.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, img.memory, nullptr);
        img.memory = VK_NULL_HANDLE;
    }
    img.extent = {};
    img.format = VK_FORMAT_UNDEFINED;
}

bool FluidRenderer::create_sampler(VkFilter filter, VkSampler& sampler) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.maxLod = 1.0f;
    return vkCreateSampler(device_, &info, nullptr, &sampler) == VK_SUCCESS;
}

bool FluidRenderer::load_shader(const char* name, VkShaderModule& out_module) {
    std::string primary = std::string(kShaderDir) + name;
    std::string fallback = std::string(kShaderDirFallback) + name;

    std::ifstream file(primary, std::ios::ate | std::ios::binary);
    if (!file) {
        file.open(fallback, std::ios::ate | std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open shader: " << primary << " or " << fallback << std::endl;
            return false;
        }
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), size);
    file.close();

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = data.size();
    info.pCode = reinterpret_cast<const uint32_t*>(data.data());
    if (vkCreateShaderModule(device_, &info, nullptr, &out_module) != VK_SUCCESS) {
        std::cerr << "Failed to create shader module: " << primary << std::endl;
        return false;
    }
    return true;
}

void FluidRenderer::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                                     VkImageLayout new_layout, VkImageAspectFlags aspect) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void FluidRenderer::barrier_compute_to_fragment(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
}

}  // namespace rayol::fluid
