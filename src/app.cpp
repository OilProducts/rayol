#include "app.h"

#include <SDL3/SDL.h>
#include <iostream>
#include <functional>
#include <cmath>
#include <algorithm>

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
constexpr float kDegToRad = 3.14159265359f / 180.0f;
}

// Simple camera state for a fly-style controller.
struct Camera {
    fluid::Vec3 position{0.0f, 0.32f, -0.8f};
    float yaw = 0.0f;    // radians, yaw around +Y
    float pitch = 0.0f;  // radians, pitch up/down
    float fov_y = 60.0f * kDegToRad;
};

static inline fluid::Vec3 cross(const fluid::Vec3& a, const fluid::Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
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
    bool rotating_camera = false;
    ui::UiState ui_state{};
    fluid::FluidExperiment fluid;
    fluid::FluidRenderer fluid_renderer;
    Camera camera;
    {
        auto ext = fluid.volume_extent();
        camera.position = {ext.x * 0.5f, ext.y * 0.55f, -ext.z * 0.9f};
        camera.yaw = 3.14159265359f * 0.5f;   // Look toward +Z by default.
        camera.pitch = -0.05f;                // Slight downward tilt.
    }

    Uint64 prev_counter = SDL_GetPerformanceCounter();
    const double perf_freq = static_cast<double>(SDL_GetPerformanceFrequency());
    float log_timer = 0.0f;

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
                break;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
                break;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_RIGHT) {
                rotating_camera = true;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_RIGHT) {
                rotating_camera = false;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && rotating_camera) {
                constexpr float kMouseSensitivity = 0.0025f;  // radians per pixel
                camera.yaw += static_cast<float>(event.motion.xrel) * kMouseSensitivity;
                camera.pitch -= static_cast<float>(event.motion.yrel) * kMouseSensitivity;
            }
            imgui_layer.process_event(event);
        }
        if (!running) {
            break;
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
                ui_state.fluid_enabled = true;  // Start with the fluid sim active.
                ui_state.fluid_paused = false;
                fluid.reset();
                fluid_frame_index = 0;
            }
        } else {  // Mode::Running
            ui::FluidUiIntents fluid_intents{};
            auto ui_callback = [&](bool& /*exit_flag*/) {
                fluid_intents = ui::render_fluid_ui(ui_state, fluid.stats());
            };

            // Camera controls: WASD move, Space/LCtrl up/down, right mouse + move to look.
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float move_speed = 1.5f;  // units per second
            camera.pitch = std::clamp(camera.pitch, -1.4f, 1.4f);

            float cy = std::cos(camera.yaw);
            float sy = std::sin(camera.yaw);
            float cp = std::cos(camera.pitch);
            float sp = std::sin(camera.pitch);
            fluid::Vec3 forward{cy * cp, sp, sy * cp};
            fluid::Vec3 world_up{0.0f, 1.0f, 0.0f};
            fluid::Vec3 right = fluid::normalize(cross(forward, world_up));
            if (right.x == 0.0f && right.y == 0.0f && right.z == 0.0f) {
                right = {1.0f, 0.0f, 0.0f};
            }
            fluid::Vec3 up = cross(right, forward);
            up = fluid::normalize(up);

            fluid::Vec3 move{0.0f, 0.0f, 0.0f};
            if (keys[SDL_SCANCODE_W]) move = move + forward;
            if (keys[SDL_SCANCODE_S]) move = move - forward;
            if (keys[SDL_SCANCODE_D]) move = move + right;
            if (keys[SDL_SCANCODE_A]) move = move - right;
            // Standard convention: +Y is up in world space.
            // SPACE moves camera up (+Y), LCTRL moves camera down (-Y).
            if (keys[SDL_SCANCODE_SPACE]) move.y += 1.0f;
            if (keys[SDL_SCANCODE_LCTRL]) move.y -= 1.0f;
            move = fluid::normalize(move);
            move = move * (move_speed * dt);
            camera.position = camera.position + move;

            FluidDrawData fluid_draw{};
            fluid_draw.renderer = &fluid_renderer;
            fluid_draw.sim = &fluid;
            fluid_draw.enabled = ui_state.fluid_enabled;
            fluid_draw.frame_index = fluid_frame_index;
            fluid_draw.density_scale = ui_state.fluid_density_scale;
            fluid_draw.absorption = ui_state.fluid_absorption;

            // Fill camera data for the renderer using the updated camera.
            fluid_draw.camera_pos = camera.position;
            fluid_draw.camera_forward = forward;
            fluid_draw.camera_right = right;
            fluid_draw.camera_fov_y = camera.fov_y;

            if (!vk.draw_frame(ui_requested_exit, ui_callback, &fluid_draw)) {
                running = false;
            }

            // Process UI intents after the frame was rendered; effects apply next frame (1-frame latency).
            fluid::FluidSettings settings{};
            settings.particle_count = ui_state.fluid_particles;
            settings.kernel_radius = ui_state.fluid_kernel_radius;
            settings.voxel_size = ui_state.fluid_voxel_size;
            settings.gravity_y = ui_state.fluid_gravity_y;
            settings.paused = ui_state.fluid_paused;
            fluid.configure(settings);
            if (fluid_intents.reset) {
                if (!fluid.particles().empty()) {
                    const auto& p = fluid.particles().front();
                    std::cerr << "[fluid] reset request: first particle before=" << p.position.x << "," << p.position.y
                              << "," << p.position.z << std::endl;
                }
                fluid.reset();
                fluid_frame_index = 0;
                ui_state.fluid_paused = false;  // Ensure motion resumes after a reset.
                if (!fluid.particles().empty()) {
                    const auto& p = fluid.particles().front();
                    std::cerr << "[fluid] reset done: first particle after=" << p.position.x << "," << p.position.y
                              << "," << p.position.z << std::endl;
                }
            }
            if (ui_state.fluid_enabled) {
                fluid.update(dt);
                fluid_frame_index++;
            }

            // Periodic debug logging to diagnose black render issues.
            log_timer += dt;
            if (log_timer >= 1.0f) {
                log_timer = 0.0f;
                const auto& stats = fluid.stats();
                std::cerr << "[fluid] stats frame=" << fluid_frame_index
                          << " particles=" << stats.particle_count
                          << " max_dens=" << stats.max_density
                          << " avg_dens=" << stats.avg_density
                          << " avg_speed=" << stats.avg_speed
                          << " max_speed=" << stats.max_speed
                          << " avg_y=" << stats.avg_height
                          << " cam_y=" << camera.position.y
                          << " dens_scale=" << ui_state.fluid_density_scale
                          << " absorb=" << ui_state.fluid_absorption
                          << " voxel=" << ui_state.fluid_voxel_size
                          << " kernel=" << ui_state.fluid_kernel_radius
                          << " enabled=" << ui_state.fluid_enabled
                          << " paused=" << ui_state.fluid_paused
                          << std::endl;
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
