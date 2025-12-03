#include "fluid_experiment.h"

#include <algorithm>
#include <random>

namespace rayol::fluid {

namespace {
constexpr int kDefaultDim = 32;
constexpr float kBounceDamping = 0.5f;
}  // namespace

FluidExperiment::FluidExperiment() {
    volume_config_.dims = {kDefaultDim, kDefaultDim, kDefaultDim};
    volume_config_.voxel_size = settings_.voxel_size;
    rebuild_volume();
    reseed_particles();
    resplat_density();
    compute_stats();
}

void FluidExperiment::configure(const FluidSettings& new_settings) {
    bool volume_changed = new_settings.voxel_size != settings_.voxel_size;
    bool particle_count_changed = new_settings.particle_count != settings_.particle_count;

    settings_ = new_settings;
    if (volume_changed) {
        volume_config_.voxel_size = settings_.voxel_size;
    }

    if (volume_changed) {
        rebuild_volume();
    }
    if (volume_changed || particle_count_changed) {
        reseed_particles();
        resplat_density();
        compute_stats();
    }
}

void FluidExperiment::reset() {
    reseed_particles();
    resplat_density();
    compute_stats();
}

void FluidExperiment::update(float dt) {
    if (settings_.paused) return;
    integrate_particles(dt);
    resplat_density();
    compute_stats();
}

void FluidExperiment::rebuild_volume() {
    volume_.resize(volume_config_);
    volume_.clear();
}

void FluidExperiment::reseed_particles() {
    particles_.clear();
    particles_.resize(settings_.particle_count);

    Vec3 ext = volume_extent();
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist_x(0.05f * ext.x, 0.95f * ext.x);
    std::uniform_real_distribution<float> dist_y(0.2f * ext.y, 0.9f * ext.y);
    std::uniform_real_distribution<float> dist_z(0.05f * ext.z, 0.95f * ext.z);

    for (auto& p : particles_) {
        p.position = {dist_x(rng), dist_y(rng), dist_z(rng)};
        p.velocity = {0.0f, 0.0f, 0.0f};
        p.radius = settings_.kernel_radius;
        p.mass = 1.0f;
    }
}

void FluidExperiment::integrate_particles(float dt) {
    Vec3 min_bound = volume_config_.origin;
    Vec3 max_bound = volume_extent();

    for (auto& p : particles_) {
        // Basic gravity along Y.
        p.velocity.y += settings_.gravity_y * dt;
        p.position = p.position + p.velocity * dt;

        // Bounce against bounds with damping.
        if (p.position.x < min_bound.x) {
            p.position.x = min_bound.x;
            p.velocity.x = -p.velocity.x * kBounceDamping;
        } else if (p.position.x > max_bound.x) {
            p.position.x = max_bound.x;
            p.velocity.x = -p.velocity.x * kBounceDamping;
        }
        if (p.position.y < min_bound.y) {
            p.position.y = min_bound.y;
            p.velocity.y = -p.velocity.y * kBounceDamping;
        } else if (p.position.y > max_bound.y) {
            p.position.y = max_bound.y;
            p.velocity.y = -p.velocity.y * kBounceDamping;
        }
        if (p.position.z < min_bound.z) {
            p.position.z = min_bound.z;
            p.velocity.z = -p.velocity.z * kBounceDamping;
        } else if (p.position.z > max_bound.z) {
            p.position.z = max_bound.z;
            p.velocity.z = -p.velocity.z * kBounceDamping;
        }
    }
}

void FluidExperiment::resplat_density() {
    volume_.clear();
    volume_.splat_particles(particles_, settings_.kernel_radius);
}

void FluidExperiment::compute_stats() {
    stats_.particle_count = static_cast<int>(particles_.size());
    stats_.max_density = 0.0f;
    stats_.avg_density = 0.0f;
    if (volume().density().empty()) return;

    float accum = 0.0f;
    for (float d : volume().density()) {
        stats_.max_density = std::max(stats_.max_density, d);
        accum += d;
    }
    stats_.avg_density = accum / static_cast<float>(volume().density().size());
}

Vec3 FluidExperiment::volume_extent() const {
    return {
        static_cast<float>(volume_config_.dims.x) * volume_config_.voxel_size,
        static_cast<float>(volume_config_.dims.y) * volume_config_.voxel_size,
        static_cast<float>(volume_config_.dims.z) * volume_config_.voxel_size,
    };
}

}  // namespace rayol::fluid
