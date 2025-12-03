#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace rayol::fluid {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Int3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 v) {
    float len = length(v);
    return (len > 0.0f) ? (v / len) : Vec3{};
}
inline Vec3 hadamard(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a * (1.0f - t) + b * t; }

struct Particle {
    Vec3 position{};
    Vec3 velocity{};
    float radius = 0.05f;  // influence radius for splatting
    float mass = 1.0f;
};

struct VolumeConfig {
    Int3 dims{32, 32, 32};
    float voxel_size = 0.02f;
    Vec3 origin{0.0f, 0.0f, 0.0f};
};

// CPU reference volume for density accumulation; GPU path will mirror this layout.
class DensityVolume {
public:
    DensityVolume() = default;
    explicit DensityVolume(const VolumeConfig& cfg) { resize(cfg); }

    void resize(const VolumeConfig& cfg);
    void clear();

    // Splat particles with a smooth kernel (poly6) to prefilter density.
    void splat_particles(const std::vector<Particle>& particles, float kernel_radius);

    // Tri-linear sample at world position; returns 0 outside the volume.
    float sample(Vec3 world_pos) const;
    // Gradient via central differences for lighting.
    Vec3 gradient(Vec3 world_pos) const;

    const VolumeConfig& config() const { return config_; }
    const std::vector<float>& density() const { return density_; }

private:
    size_t index(int x, int y, int z) const;
    Vec3 voxel_center(int x, int y, int z) const;

    VolumeConfig config_{};
    std::vector<float> density_;
};

}  // namespace rayol::fluid
