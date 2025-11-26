# Rayol

Experimental ray-driven voxel engine prototype. The goal is to explore ray-focused traversal and lighting against sparse voxel data.

## Status
- Fresh start; no implementation yet.
- Notes and prototypes will land here as the exploration proceeds.

## Build
- CMake (>=3.20) and a C++20 compiler are required.
- SDL3: CMake will look for a system SDL3; if missing it will fetch/build SDL3 from source by default. Set `-DRAYOL_BUNDLE_SDL3=OFF` to require a system install, or point `SDL3_DIR`/`CMAKE_PREFIX_PATH` at your SDL3 config.
- Vulkan: by default CMake checks for the Vulkan SDK (and `glslc` for shaders); disable this check with `-DRAYOL_ENABLE_VULKAN=OFF` if you do not have it installed yet.
- ImGui: enabled by default; set `-DRAYOL_USE_IMGUI=OFF` to skip fetching/linking it (requires Vulkan backend when on).
- Configure and build: `cmake -S . -B build && cmake --build build`.
