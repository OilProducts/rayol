#pragma once

#include <vector>

#include "fluid_sim.h"

namespace rayol::fluid {

struct FluidSettings {
    int particle_count = 512;
    float kernel_radius = 0.06f;
    float voxel_size = 0.02f;
    float gravity_y = -9.8f;
    bool paused = false;
};

struct FluidStats {
    int particle_count = 0;
    float max_density = 0.0f;
    float avg_density = 0.0f;
};

// Lightweight CPU-only prototype of the fluid sim: integrates particles, bounces off bounds, and
// splats into a density volume. Acts as a driver for the shader-based version.
class FluidExperiment {
public:
    FluidExperiment();

    // Update settings; rebuilds volume/particles if counts or layout change.
    void configure(const FluidSettings& settings);
    // Re-seed particles and clear density.
    void reset();
    // Step simulation and recompute density/stats if enabled.
    void update(float dt);

    const FluidSettings& settings() const { return settings_; }
    const FluidStats& stats() const { return stats_; }
    const DensityVolume& volume() const { return volume_; }
    const std::vector<Particle>& particles() const { return particles_; }

    Vec3 volume_extent() const;

private:
    void rebuild_volume();
    void reseed_particles();
    void integrate_particles(float dt);
    void resplat_density();
    void compute_stats();

    FluidSettings settings_{};
    FluidStats stats_{};
    VolumeConfig volume_config_{};
    DensityVolume volume_{};
    std::vector<Particle> particles_;
};

}  // namespace rayol::fluid
