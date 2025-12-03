cmake_minimum_required(VERSION 3.20)
project(rayol LANGUAGES CXX)

option(RAYOL_BUNDLE_SDL3 "Download/build SDL3 locally if not found on the system" ON)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place build outputs under bin/ and lib/ inside the build tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${CONFIG}" CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
    endforeach()
endif()

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found; install it to build Rayol.")
endif()
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found (part of Vulkan SDK) – required for shader compilation.")
endif()

# Compile fluid experiment shaders to SPIR-V.
set(rayol_fluid_shader_dir "${CMAKE_BINARY_DIR}/shaders/fluid")
set(RAYOL_FLUID_SHADER_DIR "${rayol_fluid_shader_dir}")
file(MAKE_DIRECTORY "${rayol_fluid_shader_dir}")
set(rayol_fluid_shaders
    experiments/fluid/shaders/particle_splat.comp
    experiments/fluid/shaders/volume_raymarch.frag
    experiments/fluid/shaders/fullscreen_uv.vert
)
set(rayol_fluid_spv)
foreach(shader ${rayol_fluid_shaders})
    get_filename_component(shader_name "${shader}" NAME)
    set(spv "${rayol_fluid_shader_dir}/${shader_name}.spv")
    set(shader_src "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
    add_custom_command(
        OUTPUT "${spv}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${rayol_fluid_shader_dir}"
        COMMAND ${GLSLC} -o "${spv}" "${shader_src}"
        DEPENDS "${shader_src}"
        COMMENT "Compiling ${shader_name} -> ${spv}"
        VERBATIM
    )
    list(APPEND rayol_fluid_spv "${spv}")
endforeach()
add_custom_target(rayol_fluid_shaders ALL DEPENDS ${rayol_fluid_spv})

find_package(SDL3 QUIET CONFIG)
if(NOT SDL3_FOUND)
    if(NOT RAYOL_BUNDLE_SDL3)
        message(FATAL_ERROR "SDL3 not found. Set SDL3_DIR or enable RAYOL_BUNDLE_SDL3 to fetch it.")
    endif()
    set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL3 test library" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "Disable SDL3 install" FORCE)
    set(SDL_X11 ON CACHE BOOL "Enable X11 video driver" FORCE)
    set(SDL_WAYLAND ON CACHE BOOL "Enable Wayland video driver" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.26
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

set(rayol_sources
    src/rayol.cpp
    src/app.cpp
    src/vulkan/context.cpp
    src/vulkan/device_context.cpp
    src/vulkan/swapchain.cpp
    src/vulkan/frame_sync.cpp
    src/vulkan/command_pool.cpp
    src/ui/imgui_layer.cpp
    src/ui/menu_ui.cpp
    src/ui/fluid_ui.cpp
)

# Experimental fluid module; excluded from default build but available as target.
add_subdirectory(experiments/fluid EXCLUDE_FROM_ALL)

add_executable(rayol ${rayol_sources})
add_dependencies(rayol rayol_fluid_shaders)
target_compile_definitions(rayol PUBLIC RAYOL_FLUID_SHADER_DIR=\"${rayol_fluid_shader_dir}\")

target_include_directories(rayol PRIVATE src experiments ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rayol PRIVATE SDL3::SDL3)
if(TARGET SDL3::SDL3main)
    target_link_libraries(rayol PRIVATE SDL3::SDL3main)
endif()
target_link_libraries(rayol PRIVATE Vulkan::Vulkan)
target_link_libraries(rayol PRIVATE rayol_fluid)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.0-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_SDL3 SDL_ENABLE_OLD_NAMES)
target_link_libraries(rayol PRIVATE imgui)
