#pragma once

#include <functional>

#include "fluid_sim.h"

namespace rayol::fluid {

struct Ray {
    Vec3 origin{};
    Vec3 dir{0.0f, 0.0f, 1.0f};
};

struct RayMarchSettings {
    float step = 0.01f;
    float max_distance = 5.0f;
    float density_scale = 1.0f;
    float absorption = 1.0f;
    Vec3 light_dir{-0.4f, -1.0f, -0.2f};
    Vec3 light_color{1.0f, 0.95f, 0.9f};
    float ambient = 0.1f;
};

struct RayMarchResult {
    Vec3 color{};
    float transmittance = 1.0f;
    float optical_depth = 0.0f;
    int steps = 0;
};

// CPU reference ray marcher for the density volume. Shade callback gets position, normal, and density.
RayMarchResult ray_march_volume(const DensityVolume& volume, const Ray& ray, const RayMarchSettings& settings,
                                const std::function<Vec3(Vec3 pos, Vec3 normal, float density)>& shade = {});

}  // namespace rayol::fluid
