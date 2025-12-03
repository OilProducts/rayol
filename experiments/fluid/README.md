# Fluid Simulation Experiment

Scratchpad for a future fluid/volume renderer prototype. Nothing is wired into the build yet; this space is for notes and prototype code before integrating with the main app.

## Ideas to explore
- Particle-based dynamics (e.g., SPH/PBF) for density/velocity; export density/level-set fields each frame.
- Grid/volume representation (dense block or sparse bricks) to feed a renderer, but sampled with smoothing so it never looks blocky.
- Ray-marched volume rendering with single scattering and simple shadows; TAA/jitter to hide banding.
- Ray tracing integration for surface hits once a level set/mesh is available.
- GPU-first execution (compute shaders) with CPU fallback stubs for testing.

## Immediate next steps
1) Decide on the simulation domain/scale and pick a solver variant (e.g., Position-Based Fluids).
2) Prototype a minimal compute step that updates particles into a density grid.
3) Add a thin render path that ray marches the density grid and composites with the existing swapchain.

## Approach to avoid a voxelly look
- Splat particles into the grid with a smooth kernel (e.g., SPH poly6/Gaussian) over 2–3 voxels so density is prefiltered.
- Ray march the density as a 3D texture with tri-linear or cubic filtering; add per-pixel ray jitter + TAA to kill banding.
- Compute lighting from smoothed gradients of the density (central differences) so normals aren’t tied to voxel edges.
- Store density in bricked 3D textures so we can raise local resolution without blowing up memory; shaders still filter between bricks.
- For surfaces, either build a narrow-band level set in the grid or ray trace a smooth particle SDF (smooth-min of spheres) instead of sampling raw voxels.

## Prototype code in this directory
- `fluid_sim.h/.cpp`: CPU reference for particle splatting into a density volume and sampling/gradients.
- `raymarch.h/.cpp`: CPU reference ray marcher over the density field with simple single-scattering lighting.
- `shaders/particle_splat.comp`: Vulkan compute shader stub to splat particles into a 3D texture (poly6 kernel).
- `shaders/volume_raymarch.frag`: Vulkan fragment shader stub for volume ray marching with jittered steps.
- `shaders/fullscreen_uv.vert`: Fullscreen triangle vertex shader to drive the ray marcher.
- `fluid_renderer.h/.cpp`: Vulkan bridge that uploads particles, dispatches the splat compute, and ray-marches the density into the swapchain.

## Building the experiment target
- The CMake target `rayol_fluid` is defined but excluded from the default build. Build it explicitly via `cmake --build build --target rayol_fluid`.
- Shader SPIR-V is generated into `build/shaders/fluid` during the normal `rayol` build. The runtime looks for SPIR-V relative to the binary (`../shaders/fluid`).
