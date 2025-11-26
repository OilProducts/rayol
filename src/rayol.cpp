#include <SDL3/SDL.h>
#if RAYOL_ENABLE_VULKAN
#include <SDL3/SDL_vulkan.h>
#include <cstdint>
#include <vulkan/vulkan.h>
#endif
#include <iostream>

namespace rayol {

class App {
public:
    int run() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
            return 1;
        }

        const Uint32 windowFlags = SDL_WINDOW_RESIZABLE |
#if RAYOL_ENABLE_VULKAN
                                   SDL_WINDOW_VULKAN |
#endif
                                   0;

        SDL_Window* window = SDL_CreateWindow("Rayol Prototype", 960, 540, windowFlags);
        if (!window) {
            std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

#if RAYOL_ENABLE_VULKAN
        if (!init_vulkan(window)) {
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
#endif

        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
            }
            SDL_Delay(16);
        }

#if RAYOL_ENABLE_VULKAN
        shutdown_vulkan();
#endif
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

private:
#if RAYOL_ENABLE_VULKAN
    bool init_vulkan(SDL_Window* window) {
        uint32_t extension_count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (!extensions || extension_count == 0) {
            std::cerr << "Failed to query Vulkan instance extensions: " << SDL_GetError() << std::endl;
            return false;
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Rayol Prototype";
        app_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.pEngineName = "Rayol";
        app_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = extension_count;
        create_info.ppEnabledExtensionNames = extensions;

        if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
            std::cerr << "vkCreateInstance failed." << std::endl;
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
            std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << std::endl;
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void shutdown_vulkan() {
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
#endif
};

}  // namespace rayol

int main() {
    rayol::App app;
    return app.run();
}
