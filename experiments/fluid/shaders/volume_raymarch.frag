#version 450

// Volume ray march for a prefiltered density grid.
// Expects tri-linear filtering on the 3D texture and a small per-pixel jitter (blue noise) fed in.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler3D uDensity;
layout(binding = 1) uniform sampler2D uBlueNoise;

layout(push_constant) uniform Params {
    vec4 volumeOrigin_step;   // xyz = origin, w = step
    vec4 volumeExtent_scale;  // xyz = extent, w = densityScale
    vec4 lightDir_absorb;     // xyz = light dir, w = absorption
    vec4 lightColor_ambient;  // xyz = light color, w = ambient
    float maxDistance;
    uint frameIndex;
    vec2 padding;
} params;

float sampleDensity(vec3 worldPos) {
    vec3 uvw = (worldPos - params.volumeOrigin_step.xyz) / params.volumeExtent_scale.xyz;
    return texture(uDensity, uvw).r * params.volumeExtent_scale.w;
}

vec3 gradient(vec3 worldPos, float h) {
    vec3 dx = vec3(h, 0.0, 0.0);
    vec3 dy = vec3(0.0, h, 0.0);
    vec3 dz = vec3(0.0, 0.0, h);
    float gx = sampleDensity(worldPos + dx) - sampleDensity(worldPos - dx);
    float gy = sampleDensity(worldPos + dy) - sampleDensity(worldPos - dy);
    float gz = sampleDensity(worldPos + dz) - sampleDensity(worldPos - dz);
    return vec3(gx, gy, gz) / (2.0 * h);
}

void main() {
    // Screen-aligned view: cast rays along +Z through the volume box using UV as XY.
    vec3 origin = vec3(params.volumeOrigin_step.xy + vUV * params.volumeExtent_scale.xy, params.volumeOrigin_step.z);
    vec3 dir = vec3(0.0, 0.0, 1.0);

    float jitter = texelFetch(uBlueNoise, ivec2(gl_FragCoord.xy) % textureSize(uBlueNoise, 0), 0).r;
    float stepSize = max(0.0001, params.volumeOrigin_step.w);

    vec3 accum = vec3(0.0);
    float transmittance = 1.0;
    float t = jitter * stepSize;

    vec3 ambient = vec3(params.lightColor_ambient.w);
    vec3 lightDir = normalize(params.lightDir_absorb.xyz);
    float maxDist = min(params.maxDistance, params.volumeExtent_scale.z);

    for (; t < maxDist && transmittance > 0.001; t += stepSize) {
        vec3 pos = origin + dir * t;
        float density = sampleDensity(pos);
        if (density <= 0.0) continue;

        float sigmaT = density * params.lightDir_absorb.w;
        float attenuation = exp(-sigmaT * stepSize);

        vec3 n = normalize(gradient(pos, stepSize * 0.5));
        float nDotL = max(0.0, -dot(n, lightDir));
        vec3 lighting = ambient + params.lightColor_ambient.xyz * nDotL;

        vec3 inScatter = lighting * (sigmaT * stepSize);
        accum += transmittance * inScatter;
        transmittance *= attenuation;
    }

    outColor = vec4(accum, transmittance);
}
