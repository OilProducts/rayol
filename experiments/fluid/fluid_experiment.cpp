#include "fluid_experiment.h"

#include <algorithm>
#include <random>

namespace rayol::fluid {

namespace {
constexpr int kDefaultDim = 32;
constexpr float kBounceDamping = 0.8f;
// Linear velocity damping to keep the system from gaining energy indefinitely,
// but low enough to allow visible motion and sloshing.
constexpr float kViscosity = 0.02f;

// Bounded SPH-like kernels (heuristic, not physically normalized).
inline float poly6_kernel(float r, float h) {
    if (r >= h || h <= 0.0f) return 0.0f;
    float q = 1.0f - (r / h);  // in [0,1)
    return q * q * q;          // smooth, bounded [0,1]
}

inline Vec3 spiky_gradient(const Vec3& rij, float r, float h) {
    if (r <= 0.0f || r >= h || h <= 0.0f) return {0.0f, 0.0f, 0.0f};
    float q = 1.0f - (r / h);
    // Gradient magnitude ~ q^2 / h, direction along -rij.
    float scalar = -(q * q) / (h * r);
    return rij * scalar;
}

inline float visc_laplacian(float r, float h) {
    if (r >= h || h <= 0.0f) return 0.0f;
    float q = 1.0f - (r / h);
    return q;  // simple, bounded [0,1]
}

// Pressure stiffness: larger values make the fluid less compressible.
constexpr float kPressureStiffness = 3.0f;
// Viscosity coefficient for SPH pairwise term.
constexpr float kSphViscosity = 0.01f;
// Safety clamps to keep the toy sim numerically stable.
constexpr float kMaxAccel = 200.0f;
constexpr float kMaxSpeed = 20.0f;
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
    bool kernel_radius_changed = new_settings.kernel_radius != settings_.kernel_radius;

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
    } else if (kernel_radius_changed) {
        // Re-splat and refresh stats when only the kernel radius changes.
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
    // SPH step: compute per-particle densities/pressures, then integrate.
    compute_sph_densities();
    integrate_particles(dt);

    // Rebuild density for rendering and stats after integration.
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
    densities_.assign(particles_.size(), 0.0f);
    pressures_.assign(particles_.size(), 0.0f);

    Vec3 ext = volume_extent();
    static std::mt19937 rng;
    static std::random_device rd;
    rng.seed(rd());  // New seed each reset so layouts change visibly.
    std::uniform_real_distribution<float> dist_x(0.1f * ext.x, 0.9f * ext.x);
    std::uniform_real_distribution<float> dist_y(0.4f * ext.y, 0.9f * ext.y);
    std::uniform_real_distribution<float> dist_z(0.1f * ext.z, 0.9f * ext.z);
    std::uniform_real_distribution<float> dist_v(-1.5f, 1.5f);

    for (auto& p : particles_) {
        p.position = {dist_x(rng), dist_y(rng), dist_z(rng)};
        p.velocity = {dist_v(rng), dist_v(rng) * 0.5f, dist_v(rng)};
        p.radius = settings_.kernel_radius;
        p.mass = 1.0f;
    }
}

void FluidExperiment::integrate_particles(float dt) {
    Vec3 min_bound = volume_config_.origin;
    Vec3 max_bound = volume_extent();
    float floor_y = min_bound.y + settings_.kernel_radius * 0.5f;

    const size_t n = particles_.size();
    if (n == 0) return;

    float h = settings_.kernel_radius;
    if (h <= 0.0f) h = 0.01f;

    std::vector<Vec3> forces(n, Vec3{0.0f, 0.0f, 0.0f});

    // Base forces: gravity and simple linear drag.
    for (size_t i = 0; i < n; ++i) {
        Vec3 accel{0.0f, settings_.gravity_y, 0.0f};
        Vec3 drag{-kViscosity * particles_[i].velocity.x,
                  -kViscosity * particles_[i].velocity.y,
                  -kViscosity * particles_[i].velocity.z};
        accel = accel + drag;
        forces[i] = accel;
    }

    // SPH pairwise pressure + viscosity.
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            Vec3 rij = particles_[i].position - particles_[j].position;
            float r = length(rij);
            if (r <= 0.0f || r >= h) continue;

            float rho_i = densities_[i];
            float rho_j = densities_[j];
            if (rho_i <= 0.0f || rho_j <= 0.0f) continue;

            // Pressure force (symmetric).
            float p_i = pressures_[i];
            float p_j = pressures_[j];
            float p_term = (p_i + p_j) * 0.5f;
            if (p_term > 0.0f) {
                Vec3 gradW = spiky_gradient(rij, r, h);
                // Scale by inverse densities to reduce sensitivity to absolute density.
                Vec3 f = gradW * (-p_term / (rho_i * rho_j));
                forces[i] = forces[i] + f;
                forces[j] = forces[j] - f;
            }

            // Viscosity force (symmetric).
            Vec3 vel_diff = particles_[j].velocity - particles_[i].velocity;
            float lap = visc_laplacian(r, h);
            if (lap > 0.0f) {
                Vec3 f_visc = vel_diff * (kSphViscosity * lap / rho_j);
                forces[i] = forces[i] + f_visc;
                forces[j] = forces[j] - f_visc;
            }
        }
    }

    // Integrate and handle bounds.
    for (size_t i = 0; i < n; ++i) {
        Vec3 accel = forces[i];
        float a_len = length(accel);
        if (!std::isfinite(a_len) || a_len <= 0.0f) {
            accel = {0.0f, 0.0f, 0.0f};
        } else if (a_len > kMaxAccel) {
            accel = accel * (kMaxAccel / a_len);
        }

        particles_[i].velocity = particles_[i].velocity + accel * dt;

        float v_len = length(particles_[i].velocity);
        if (!std::isfinite(v_len) || v_len <= 0.0f) {
            particles_[i].velocity = {0.0f, 0.0f, 0.0f};
        } else if (v_len > kMaxSpeed) {
            particles_[i].velocity = particles_[i].velocity * (kMaxSpeed / v_len);
        }

        particles_[i].position = particles_[i].position + particles_[i].velocity * dt;

        auto& p = particles_[i];
        if (p.position.x < min_bound.x) {
            p.position.x = min_bound.x;
            p.velocity.x = -p.velocity.x * kBounceDamping;
        } else if (p.position.x > max_bound.x) {
            p.position.x = max_bound.x;
            p.velocity.x = -p.velocity.x * kBounceDamping;
        }
        if (p.position.y < floor_y) {
            p.position.y = floor_y;
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

void FluidExperiment::compute_sph_densities() {
    const size_t n = particles_.size();
    densities_.assign(n, 0.0f);
    pressures_.assign(n, 0.0f);
    rest_density_ = 0.0f;

    if (n == 0) return;

    float h = settings_.kernel_radius;
    if (h <= 0.0f) h = 0.01f;

    // Compute per-particle density using poly6 kernel.
    for (size_t i = 0; i < n; ++i) {
        float rho = 0.0f;
        for (size_t j = 0; j < n; ++j) {
            Vec3 rij = particles_[i].position - particles_[j].position;
            float r = length(rij);
            rho += particles_[j].mass * poly6_kernel(r, h);
        }
        densities_[i] = rho;
        rest_density_ += rho;
    }

    rest_density_ /= static_cast<float>(n);

    if (rest_density_ <= 0.0f) {
        std::fill(pressures_.begin(), pressures_.end(), 0.0f);
        return;
    }

    // Compute pressures from densities.
    for (size_t i = 0; i < n; ++i) {
        float rho = densities_[i];
        float compression = (rho - rest_density_) / rest_density_;
        pressures_[i] = (compression > 0.0f)
                            ? (kPressureStiffness * compression)
                            : 0.0f;
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
    stats_.max_speed = 0.0f;
    stats_.avg_speed = 0.0f;
    stats_.avg_height = 0.0f;
    if (volume().density().empty()) return;

    float accum = 0.0f;
    for (float d : volume().density()) {
        stats_.max_density = std::max(stats_.max_density, d);
        accum += d;
    }
    stats_.avg_density = accum / static_cast<float>(volume().density().size());

    if (!particles_.empty()) {
        float speed_accum = 0.0f;
        float height_accum = 0.0f;
        for (const auto& p : particles_) {
            float s = length(p.velocity);
            stats_.max_speed = std::max(stats_.max_speed, s);
            speed_accum += s;
            height_accum += p.position.y;
        }
        stats_.avg_speed = speed_accum / static_cast<float>(particles_.size());
        stats_.avg_height = height_accum / static_cast<float>(particles_.size());
    }
}

Vec3 FluidExperiment::volume_extent() const {
    return {
        static_cast<float>(volume_config_.dims.x) * volume_config_.voxel_size,
        static_cast<float>(volume_config_.dims.y) * volume_config_.voxel_size,
        static_cast<float>(volume_config_.dims.z) * volume_config_.voxel_size,
    };
}

}  // namespace rayol::fluid
