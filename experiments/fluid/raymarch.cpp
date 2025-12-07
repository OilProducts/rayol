#include "raymarch.h"

#include <algorithm>
#include <cmath>

namespace rayol::fluid {

RayMarchResult ray_march_volume(const DensityVolume& volume, const Ray& input_ray, const RayMarchSettings& settings,
                                const std::function<Vec3(Vec3, Vec3, float)>& shade) {
    Ray ray = input_ray;
    ray.dir = normalize(ray.dir);

    // Compute intersection of ray with the volume's axis-aligned bounds and clamp marching to that range.
    const VolumeConfig& cfg = volume.config();
    Vec3 box_min{cfg.origin.x,
                 cfg.origin.y,
                 cfg.origin.z};
    Vec3 box_max{cfg.origin.x + static_cast<float>(cfg.dims.x) * cfg.voxel_size,
                 cfg.origin.y + static_cast<float>(cfg.dims.y) * cfg.voxel_size,
                 cfg.origin.z + static_cast<float>(cfg.dims.z) * cfg.voxel_size};

    Vec3 inv_dir{1.0f / ray.dir.x, 1.0f / ray.dir.y, 1.0f / ray.dir.z};
    Vec3 t0{(box_min.x - ray.origin.x) * inv_dir.x,
            (box_min.y - ray.origin.y) * inv_dir.y,
            (box_min.z - ray.origin.z) * inv_dir.z};
    Vec3 t1{(box_max.x - ray.origin.x) * inv_dir.x,
            (box_max.y - ray.origin.y) * inv_dir.y,
            (box_max.z - ray.origin.z) * inv_dir.z};
    Vec3 tmin{std::min(t0.x, t1.x), std::min(t0.y, t1.y), std::min(t0.z, t1.z)};
    Vec3 tmax{std::max(t0.x, t1.x), std::max(t0.y, t1.y), std::max(t0.z, t1.z)};
    float t_enter = std::max(std::max(tmin.x, tmin.y), tmin.z);
    float t_exit = std::min(std::min(tmax.x, tmax.y), tmax.z);
    if (t_exit <= t_enter) {
        return {};
    }

    Vec3 light_dir = normalize(settings.light_dir);
    float step = std::max(0.0001f, settings.step);

    Vec3 accum_color{};
    float transmittance = 1.0f;
    float optical = 0.0f;
    int steps = 0;

    float t_start = std::max(0.0f, t_enter);
    float t_end = std::min(t_exit, t_start + settings.max_distance);

    for (float t = t_start; t < t_end && transmittance > 0.001f; t += step, ++steps) {
        Vec3 pos = ray.origin + ray.dir * t;
        float density = volume.sample(pos) * settings.density_scale;
        if (density <= 0.0f) {
            continue;
        }

        float sigma_t = density * settings.absorption;
        float attenuation = std::exp(-sigma_t * step);
        optical += sigma_t * step;

        Vec3 normal = normalize(volume.gradient(pos));
        Vec3 surface_light{};
        if (shade) {
            surface_light = shade(pos, normal, density);
        } else {
            float n_dot_l = std::max(0.0f, -dot(normal, light_dir));
            Vec3 ambient = {settings.ambient, settings.ambient, settings.ambient};
            surface_light = settings.light_color * n_dot_l + ambient;
        }

        Vec3 in_scatter = surface_light * (sigma_t * step);
        accum_color = accum_color + transmittance * in_scatter;
        transmittance *= attenuation;
    }

    return {.color = accum_color, .transmittance = transmittance, .optical_depth = optical, .steps = steps};
}

}  // namespace rayol::fluid
