#include "app.h"

#include <SDL3/SDL.h>
#include <iostream>
#include <functional>

#include "vulkan/context.h"
#include "experiments/fluid/fluid_experiment.h"
#include "experiments/fluid/fluid_renderer.h"
#include "ui/imgui_layer.h"
#include "ui/fluid_ui.h"
#include "ui/menu_ui.h"
#include "ui/ui_models.h"

namespace rayol {

namespace {
constexpr Uint32 kWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;
}

// Initialize systems and drive the main loop with mode switching.
int App::run() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Rayol Prototype", 960, 540, kWindowFlags);
    if (!window) {
        std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    Mode mode = Mode::MainMenu;

    VulkanContext vk;
    if (!vk.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ImGuiLayer imgui_layer;
    ImGuiLayer::InitInfo imgui_info{};
    imgui_info.window = window;
    imgui_info.instance = vk.instance();
    imgui_info.physical_device = vk.physical_device();
    imgui_info.device = vk.device();
    imgui_info.queue_family = vk.queue_family_index();
    imgui_info.queue = vk.queue();
    imgui_info.descriptor_pool = vk.descriptor_pool();
    imgui_info.min_image_count = vk.min_image_count();
    imgui_info.render_pass = vk.render_pass();

    if (!imgui_layer.init(imgui_info)) {
        vk.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    vk.set_imgui_layer(&imgui_layer);

    bool running = true;
    ui::UiState ui_state{};
    fluid::FluidExperiment fluid;
    fluid::FluidRenderer fluid_renderer;

    Uint64 prev_counter = SDL_GetPerformanceCounter();
    const double perf_freq = static_cast<double>(SDL_GetPerformanceFrequency());

    if (!fluid_renderer.init(vk.physical_device(), vk.device(), vk.queue_family_index(), vk.queue(),
                             vk.descriptor_pool(), vk.render_pass(), vk.swapchain_extent(), vk.atomic_float_enabled())) {
        std::cerr << "Failed to init fluid renderer." << std::endl;
        imgui_layer.shutdown();
        vk.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    uint32_t fluid_frame_index = 0;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>((now - prev_counter) / perf_freq);
        prev_counter = now;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            imgui_layer.process_event(event);
        }

        bool ui_requested_exit = false;
        if (mode == Mode::MainMenu) {
            ui::MenuIntents menu_intents{};
            auto ui_callback = [&](bool& exit_flag) {
                menu_intents = ui::render_menu_ui(ui_state);
                exit_flag = exit_flag || menu_intents.exit;
            };
            if (!vk.draw_frame(ui_requested_exit, ui_callback, nullptr)) {
                running = false;
            }
            if (menu_intents.start) {
                mode = Mode::Running;
            }
        } else {  // Mode::Running
            ui::FluidUiIntents fluid_intents{};
            auto ui_callback = [&](bool& /*exit_flag*/) {
                fluid_intents = ui::render_fluid_ui(ui_state, fluid.stats());
            };

            fluid::FluidSettings settings{};
            settings.particle_count = ui_state.fluid_particles;
            settings.kernel_radius = ui_state.fluid_kernel_radius;
            settings.voxel_size = ui_state.fluid_voxel_size;
            settings.gravity_y = ui_state.fluid_gravity_y;
            settings.paused = ui_state.fluid_paused;
            fluid.configure(settings);
            if (fluid_intents.reset) {
                fluid.reset();
            }
            if (ui_state.fluid_enabled) {
                fluid.update(dt);
                fluid_frame_index++;
            }

            FluidDrawData fluid_draw{};
            fluid_draw.renderer = &fluid_renderer;
            fluid_draw.sim = &fluid;
            fluid_draw.enabled = ui_state.fluid_enabled;
            fluid_draw.frame_index = fluid_frame_index;
            fluid_draw.density_scale = ui_state.fluid_density_scale;
            fluid_draw.absorption = ui_state.fluid_absorption;

            if (!vk.draw_frame(ui_requested_exit, ui_callback, &fluid_draw)) {
                running = false;
            }
        }
        if (ui_requested_exit) {
            running = false;
        }
    }

    fluid_renderer.cleanup();
    imgui_layer.shutdown();
    vk.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace rayol
