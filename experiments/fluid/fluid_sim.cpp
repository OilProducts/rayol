#include "fluid_sim.h"

#include <algorithm>

namespace rayol::fluid {

namespace {
float poly6(float r, float h) {
    if (r >= h || h <= 0.0f) return 0.0f;
    float h2 = h * h;
    float r2 = r * r;
    float term = h2 - r2;
    // Normalized poly6 kernel constant: 315 / (64 * pi * h^9).
    constexpr float k = 315.0f / (64.0f * 3.14159265359f);
    return k * term * term * term / (h2 * h2 * h2 * h);
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

void DensityVolume::resize(const VolumeConfig& cfg) {
    config_ = cfg;
    size_t total = static_cast<size_t>(config_.dims.x) * config_.dims.y * config_.dims.z;
    density_.assign(total, 0.0f);
}

void DensityVolume::clear() { std::fill(density_.begin(), density_.end(), 0.0f); }

size_t DensityVolume::index(int x, int y, int z) const {
    return static_cast<size_t>(z * config_.dims.y * config_.dims.x + y * config_.dims.x + x);
}

Vec3 DensityVolume::voxel_center(int x, int y, int z) const {
    return {
        config_.origin.x + (static_cast<float>(x) + 0.5f) * config_.voxel_size,
        config_.origin.y + (static_cast<float>(y) + 0.5f) * config_.voxel_size,
        config_.origin.z + (static_cast<float>(z) + 0.5f) * config_.voxel_size,
    };
}

void DensityVolume::splat_particles(const std::vector<Particle>& particles, float kernel_radius) {
    if (density_.empty()) return;
    float h = kernel_radius;
    for (const auto& p : particles) {
        float influence = std::max(h, p.radius);
        // Compute the voxel bounds overlapped by the kernel.
        Vec3 min_p = {p.position.x - influence, p.position.y - influence, p.position.z - influence};
        Vec3 max_p = {p.position.x + influence, p.position.y + influence, p.position.z + influence};

        auto to_voxel = [&](float coord, float origin_axis) {
            return static_cast<int>(std::floor((coord - origin_axis) / config_.voxel_size));
        };

        int min_x = to_voxel(min_p.x, config_.origin.x);
        int min_y = to_voxel(min_p.y, config_.origin.y);
        int min_z = to_voxel(min_p.z, config_.origin.z);
        int max_x = to_voxel(max_p.x, config_.origin.x);
        int max_y = to_voxel(max_p.y, config_.origin.y);
        int max_z = to_voxel(max_p.z, config_.origin.z);

        min_x = std::max(0, min_x);
        min_y = std::max(0, min_y);
        min_z = std::max(0, min_z);
        max_x = std::min(config_.dims.x - 1, max_x);
        max_y = std::min(config_.dims.y - 1, max_y);
        max_z = std::min(config_.dims.z - 1, max_z);

        for (int z = min_z; z <= max_z; ++z) {
            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    Vec3 c = voxel_center(x, y, z);
                    float r = length(p.position - c);
                    float w = poly6(r, influence);
                    density_[index(x, y, z)] += p.mass * w;
                }
            }
        }
    }
}

float DensityVolume::sample(Vec3 world_pos) const {
    if (density_.empty()) return 0.0f;
    Vec3 rel = {world_pos.x - config_.origin.x,
                world_pos.y - config_.origin.y,
                world_pos.z - config_.origin.z};

    float fx = rel.x / config_.voxel_size - 0.5f;
    float fy = rel.y / config_.voxel_size - 0.5f;
    float fz = rel.z / config_.voxel_size - 0.5f;

    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    int z0 = static_cast<int>(std::floor(fz));
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    float tx = clamp01(fx - static_cast<float>(x0));
    float ty = clamp01(fy - static_cast<float>(y0));
    float tz = clamp01(fz - static_cast<float>(z0));

    auto fetch = [&](int xi, int yi, int zi) -> float {
        if (xi < 0 || yi < 0 || zi < 0 || xi >= config_.dims.x || yi >= config_.dims.y || zi >= config_.dims.z) {
            return 0.0f;
        }
        return density_[index(xi, yi, zi)];
    };

    float c000 = fetch(x0, y0, z0);
    float c100 = fetch(x1, y0, z0);
    float c010 = fetch(x0, y1, z0);
    float c110 = fetch(x1, y1, z0);
    float c001 = fetch(x0, y0, z1);
    float c101 = fetch(x1, y0, z1);
    float c011 = fetch(x0, y1, z1);
    float c111 = fetch(x1, y1, z1);

    float c00 = c000 * (1.0f - tx) + c100 * tx;
    float c10 = c010 * (1.0f - tx) + c110 * tx;
    float c01 = c001 * (1.0f - tx) + c101 * tx;
    float c11 = c011 * (1.0f - tx) + c111 * tx;

    float c0 = c00 * (1.0f - ty) + c10 * ty;
    float c1 = c01 * (1.0f - ty) + c11 * ty;

    return c0 * (1.0f - tz) + c1 * tz;
}

Vec3 DensityVolume::gradient(Vec3 world_pos) const {
    float h = config_.voxel_size;
    Vec3 dx = {h, 0.0f, 0.0f};
    Vec3 dy = {0.0f, h, 0.0f};
    Vec3 dz = {0.0f, 0.0f, h};

    float gx = sample(world_pos + dx) - sample(world_pos - dx);
    float gy = sample(world_pos + dy) - sample(world_pos - dy);
    float gz = sample(world_pos + dz) - sample(world_pos - dz);
    return {gx / (2.0f * h), gy / (2.0f * h), gz / (2.0f * h)};
}

}  // namespace rayol::fluid
