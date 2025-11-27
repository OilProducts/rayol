#include "vulkan/swapchain.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

namespace rayol {

Swapchain::~Swapchain() = default;

bool Swapchain::init(DeviceContext& device, SDL_Window* window) {
    if (!create_swapchain(device, window)) return false;
    if (!create_image_views(device)) return false;
    if (!create_render_pass(device)) return false;
    if (!create_framebuffers(device)) return false;
    return true;
}

// Recreate swapchain and dependent resources (e.g., after resize).
bool Swapchain::recreate(DeviceContext& device, SDL_Window* window) {
    cleanup(device);
    return init(device, window);
}

// Release swapchain, views, framebuffers, and render pass.
void Swapchain::cleanup(DeviceContext& device) {
    for (auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        }
    }
    framebuffers_.clear();

    for (auto view : views_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device.device(), view, nullptr);
        }
    }
    views_.clear();
    images_.clear();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

// Create swapchain and fetch swapchain images.
bool Swapchain::create_swapchain(DeviceContext& device, SDL_Window* window) {
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical_device(), device.surface(), &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical_device(), device.surface(), &format_count, nullptr);
    if (format_count == 0) {
        std::cerr << "No surface formats available." << std::endl;
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical_device(), device.surface(), &format_count, formats.data());

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical_device(), device.surface(), &present_mode_count, nullptr);
    if (present_mode_count == 0) {
        std::cerr << "No present modes available." << std::endl;
        return false;
    }
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical_device(), device.surface(), &present_mode_count, present_modes.data());

    VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    VkPresentModeKHR present_mode = choose_present_mode(present_modes);
    extent_ = choose_extent(window, capabilities);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = device.surface();
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device.device(), &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain." << std::endl;
        return false;
    }

    vkGetSwapchainImagesKHR(device.device(), swapchain_, &image_count, nullptr);
    images_.resize(image_count);
    vkGetSwapchainImagesKHR(device.device(), swapchain_, &image_count, images_.data());

    format_ = surface_format.format;
    return true;
}

// Create image views for each swapchain image.
bool Swapchain::create_image_views(DeviceContext& device) {
    views_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_;
        view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.device(), &view_info, nullptr, &views_[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create image views." << std::endl;
            return false;
        }
    }
    return true;
}

// Create a simple color-only render pass.
bool Swapchain::create_render_pass(DeviceContext& device) {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device.device(), &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        std::cerr << "Failed to create render pass." << std::endl;
        return false;
    }
    return true;
}

// Create framebuffers for each swapchain view.
bool Swapchain::create_framebuffers(DeviceContext& device) {
    framebuffers_.resize(views_.size());
    for (size_t i = 0; i < views_.size(); ++i) {
        VkImageView attachments[] = {views_[i]};

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = extent_.width;
        framebuffer_info.height = extent_.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(device.device(), &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer." << std::endl;
            return false;
        }
    }
    return true;
}

VkSurfaceFormatKHR Swapchain::choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR Swapchain::choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Choose swapchain extent from surface caps and window size.
VkExtent2D Swapchain::choose_extent(SDL_Window* window, const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    VkExtent2D actual_extent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
    };

    actual_extent.width = std::max(capabilities.minImageExtent.width,
                                   std::min(capabilities.maxImageExtent.width, actual_extent.width));
    actual_extent.height = std::max(capabilities.minImageExtent.height,
                                    std::min(capabilities.maxImageExtent.height, actual_extent.height));
    return actual_extent;
}

}  // namespace rayol
