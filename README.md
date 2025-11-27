# Rayol

Experimental ray-driven voxel engine prototype. The goal is to explore ray-focused traversal and lighting against sparse voxel data.

## Status
- Prototype opens a Vulkan window with ImGui-driven menu (start/exit and basic settings).
- Rendering is currently a clear pass with UI; ray-based rendering to come next.

## Build
- CMake (>=3.20) and a C++20 compiler are required.
- SDL3: CMake will look for a system SDL3; if missing it will fetch/build SDL3 from source by default. Set `-DRAYOL_BUNDLE_SDL3=OFF` to require a system install, or point `SDL3_DIR`/`CMAKE_PREFIX_PATH` at your SDL3 config.
- Vulkan: CMake requires the Vulkan SDK (and `glslc` for shaders); install it before configuring the build.
- ImGui: always built and linked with the Vulkan backend; no opt-out toggle.
- Configure and build: `cmake -S . -B build && cmake --build build`.
