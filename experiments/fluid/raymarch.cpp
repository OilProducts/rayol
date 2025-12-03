#include "raymarch.h"

#include <algorithm>
#include <cmath>

namespace rayol::fluid {

RayMarchResult ray_march_volume(const DensityVolume& volume, const Ray& input_ray, const RayMarchSettings& settings,
                                const std::function<Vec3(Vec3, Vec3, float)>& shade) {
    Ray ray = input_ray;
    ray.dir = normalize(ray.dir);

    Vec3 light_dir = normalize(settings.light_dir);
    float step = std::max(0.0001f, settings.step);

    Vec3 accum_color{};
    float transmittance = 1.0f;
    float optical = 0.0f;
    int steps = 0;

    for (float t = 0.0f; t < settings.max_distance && transmittance > 0.001f; t += step, ++steps) {
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
