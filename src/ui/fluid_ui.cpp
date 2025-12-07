#include "ui/fluid_ui.h"

#include <imgui.h>

namespace rayol::ui {

FluidUiIntents render_fluid_ui(UiState& state, const fluid::FluidStats& stats) {
    FluidUiIntents intents{};

    ImGui::Begin("Fluid Experiment");
    ImGui::Checkbox("Enable fluid preview", &state.fluid_enabled);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        intents.reset = true;
    }

    ImGui::BeginDisabled(!state.fluid_enabled);
    ImGui::Checkbox("Paused", &state.fluid_paused);
    ImGui::SliderInt("Particles", &state.fluid_particles, 64, 4096);
    ImGui::SliderFloat("Kernel radius", &state.fluid_kernel_radius, 0.01f, 0.2f, "%.3f");
    ImGui::SliderFloat("Voxel size", &state.fluid_voxel_size, 0.01f, 0.05f, "%.3f");
    ImGui::SliderFloat("Gravity Y", &state.fluid_gravity_y, -20.0f, 0.0f, "%.2f");
    // Higher ceilings make the volume visible on typical GPUs; defaults are set in UiState.
    ImGui::SliderFloat("Density scale", &state.fluid_density_scale, 0.1f, 200.0f, "%.2f");
    ImGui::SliderFloat("Absorption", &state.fluid_absorption, 0.1f, 50.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Particles: %d", stats.particle_count);
    ImGui::Text("Max density: %.4f", stats.max_density);
    ImGui::Text("Avg density: %.4f", stats.avg_density);
    ImGui::Text("Avg speed: %.4f", stats.avg_speed);
    ImGui::Text("Max speed: %.4f", stats.max_speed);
    ImGui::EndDisabled();
    ImGui::End();

    return intents;
}

}  // namespace rayol::ui
