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
    vec4 camera_pos;          // xyz = camera position, w unused
    vec4 camera_forward;      // xyz = forward, w = tan(fov/2)
    vec4 camera_right;        // xyz = right, w = aspect
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
    // Ray from camera through pixel using a simple pinhole camera.
    // vUV already matches clip-space orientation; no additional Y flip is needed.
    vec2 uv = vUV;
    vec2 ndc = uv * 2.0 - 1.0;

    float tanHalfFov = params.camera_forward.w;
    float aspect = params.camera_right.w;
    vec3 forward = normalize(params.camera_forward.xyz);
    vec3 right = normalize(params.camera_right.xyz);
    vec3 up = normalize(cross(right, forward));
    vec3 dir = normalize(forward + ndc.x * aspect * tanHalfFov * right + ndc.y * tanHalfFov * up);
    vec3 origin = params.camera_pos.xyz;

    // Compute entry/exit distances with the axis-aligned volume box.
    vec3 boxMin = params.volumeOrigin_step.xyz;
    vec3 boxMax = boxMin + params.volumeExtent_scale.xyz;
    vec3 invDir = 1.0 / dir;
    vec3 t0 = (boxMin - origin) * invDir;
    vec3 t1 = (boxMax - origin) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);
    if (tExit <= tEnter) {
        discard;
    }

    float t = max(tEnter, 0.0);

    float jitter = texelFetch(uBlueNoise, ivec2(gl_FragCoord.xy) % textureSize(uBlueNoise, 0), 0).r;
    float stepSize = max(0.0001, params.volumeOrigin_step.w);

    vec3 accum = vec3(0.0);
    float transmittance = 1.0;
    t += jitter * stepSize;

    vec3 ambient = vec3(params.lightColor_ambient.w);
    vec3 lightDir = normalize(params.lightDir_absorb.xyz);
    float maxDist = min(params.maxDistance, tExit - tEnter);

    for (; t < tExit && transmittance > 0.001; t += stepSize) {
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
