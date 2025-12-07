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

// Simple world-space XZ grid on the plane y = 0 to provide a reference frame.
// Returns a grid color (always non-zero when the plane is hit within range).
vec3 renderGrid(vec3 origin, vec3 dir) {
    // Intersect ray with y=0 plane.
    float denom = dir.y;
    vec3 base = vec3(0.05, 0.06, 0.08);
    if (abs(denom) < 1e-4) {
        // Ray is almost parallel to the plane; just show base color.
        return base;
    }
    float tPlane = -origin.y / denom;
    if (tPlane <= 0.0) {
        // Plane is behind the camera; show base color.
        return base;
    }

    vec3 pos = origin + dir * tPlane;

    // Limit grid to a finite area so it doesn't dominate the view.
    float maxRange = 10.0;
    if (abs(pos.x) > maxRange || abs(pos.z) > maxRange) {
        return base;
    }

    float scale = 0.1;  // world units per cell (finer grid)
    vec2 g = pos.xz / scale;
    vec2 cell = abs(fract(g) - 0.5);
    vec2 fw = fwidth(g * 10.0);  // widen lines a bit
    fw = max(fw, vec2(1e-3));
    float line = min(cell.x / fw.x, cell.y / fw.y);
    float gridLine = 1.0 - clamp(line, 0.0, 1.0);

    vec3 gridColor = vec3(0.70, 0.75, 0.85);

    // Highlight axes near x=0 and z=0.
    float axisX = exp(-pos.x * pos.x * 5.0);
    float axisZ = exp(-pos.z * pos.z * 5.0);
    vec3 axisColor = vec3(0.9, 0.3, 0.3) * axisX + vec3(0.3, 0.9, 0.3) * axisZ;

    return base + gridColor * gridLine + axisColor;
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

    // March the ray: first try to find an iso-surface; if none is found,
    // fall back to a fog-style volume integration. A world-space grid is always
    // present as a background reference.
    float jitter = texelFetch(uBlueNoise, ivec2(gl_FragCoord.xy) % textureSize(uBlueNoise, 0), 0).r;
    float stepSize = max(0.0001, params.volumeOrigin_step.w);
    float iso = 0.35;  // Tunable iso-threshold in scaled density units.

    float t = max(tEnter, 0.0) + jitter * stepSize;
    bool hit = false;
    vec3 hitPos = vec3(0.0);
    vec3 hitNormal = vec3(0.0);

    // Iso-surface search.
    for (; t < tExit; t += stepSize) {
        vec3 pos = origin + dir * t;
        float density = sampleDensity(pos);
        if (density >= iso) {
            hitPos = pos;
            hitNormal = normalize(gradient(hitPos, stepSize * 0.5));
            hit = true;
            break;
        }
    }

    vec3 gridColor = renderGrid(origin, dir);
    vec3 ambientColor = vec3(params.lightColor_ambient.w);
    vec3 lightDir = normalize(params.lightDir_absorb.xyz);

    if (hit) {
        // Simple lit surface shading at the iso-surface.
        vec3 N = (length(hitNormal) > 0.0) ? normalize(hitNormal) : vec3(0.0, 1.0, 0.0);
        vec3 L = normalize(-params.lightDir_absorb.xyz);      // light from opposite of lightDir
        vec3 V = normalize(origin - hitPos);

        vec3 baseColor = vec3(0.12, 0.65, 0.95);             // bluish liquid
        vec3 lightColor = params.lightColor_ambient.xyz;
        float ambient = params.lightColor_ambient.w;

        float NdotL = max(0.0, dot(N, L));
        vec3 diffuse = baseColor * lightColor * NdotL;

        vec3 H = normalize(L + V);
        float NdotH = max(0.0, dot(N, H));
        float spec = pow(NdotH, 64.0);
        vec3 specularColor = vec3(1.0);

        // Simple Fresnel term to give a glancing-edge highlight.
        float VdotN = max(0.0, dot(V, N));
        float fresnel = mix(0.02, 1.0, pow(1.0 - VdotN, 3.0));

        vec3 fluidColor = ambient * baseColor + diffuse + spec * specularColor * fresnel;
        // Fluid surface is opaque; it completely covers the grid where present.
        outColor = vec4(fluidColor, 1.0);
        return;
    }

    // Fog-style fallback: integrate density along the ray.
    vec3 accum = vec3(0.0);
    float transmittance = 1.0;

    t = max(tEnter, 0.0) + jitter * stepSize;
    for (; t < tExit && transmittance > 0.001; t += stepSize) {
        vec3 pos = origin + dir * t;
        float density = sampleDensity(pos);
        if (density <= 0.0) continue;

        float sigmaT = density * params.lightDir_absorb.w;
        float attenuation = exp(-sigmaT * stepSize);

        vec3 n = normalize(gradient(pos, stepSize * 0.5));
        float nDotL = max(0.0, -dot(n, lightDir));
        vec3 lighting = ambientColor + params.lightColor_ambient.xyz * nDotL;

        vec3 inScatter = lighting * (sigmaT * stepSize);
        accum += transmittance * inScatter;
        transmittance *= attenuation;
    }

    // Composite fog in front of the grid: grid attenuated by transmittance.
    vec3 color = gridColor * transmittance + accum;
    outColor = vec4(color, 1.0);
}
