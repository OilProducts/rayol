#pragma once

namespace rayol::ui {

struct UiState {
    int gfx_quality = 2;       // Graphics quality selection (0=Low...3=Ultra)
    float master_volume = 0.8f;  // Master volume slider value (0..1)
    bool vsync = true;         // VSync toggle

    // Fluid experiment controls.
    bool fluid_enabled = false;     // Toggle fluid experiment visibility/sim
    bool fluid_paused = false;      // Pause the sim
    int fluid_particles = 512;      // Particle count
    float fluid_kernel_radius = 0.06f;  // Splat kernel radius
    float fluid_voxel_size = 0.02f;     // Voxel size for density volume
    float fluid_gravity_y = -9.8f;      // Gravity along Y
    float fluid_density_scale = 1.0f;   // Render density multiplier
    float fluid_absorption = 1.0f;      // Absorption coefficient
};

struct MenuIntents {
    bool start = false;        // Request to enter the running mode
    bool exit = false;         // Request to quit the app
    bool open_options = false; // Reserved for future options menu
};

struct GameIntents {
    bool exit_to_menu = false; // Request to return to main menu
    bool quit = false;         // Request to quit entirely
};

}  // namespace rayol::ui
