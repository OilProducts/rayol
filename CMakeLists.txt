cmake_minimum_required(VERSION 3.20)
project(rayol LANGUAGES CXX)

option(RAYOL_BUNDLE_SDL3 "Download/build SDL3 locally if not found on the system" ON)
option(RAYOL_ENABLE_VULKAN "Enable Vulkan dependency check" ON)
option(RAYOL_USE_IMGUI "Build with Dear ImGui UI layer" ON)

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

if(RAYOL_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan SDK not found; install it or set RAYOL_ENABLE_VULKAN=OFF.")
    endif()
    find_program(GLSLC glslc)
    if(NOT GLSLC)
        message(FATAL_ERROR "glslc not found (part of Vulkan SDK) – required for shader compilation.")
    endif()
endif()

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
    src/ui/imgui_layer.cpp
)

add_executable(rayol ${rayol_sources})

target_include_directories(rayol PRIVATE src)

target_compile_definitions(rayol PRIVATE
    RAYOL_ENABLE_VULKAN=$<BOOL:${RAYOL_ENABLE_VULKAN}>
    RAYOL_USE_IMGUI=$<BOOL:${RAYOL_USE_IMGUI}>
)

target_link_libraries(rayol PRIVATE SDL3::SDL3)
if(TARGET SDL3::SDL3main)
    target_link_libraries(rayol PRIVATE SDL3::SDL3main)
endif()
if(RAYOL_ENABLE_VULKAN)
    target_link_libraries(rayol PRIVATE Vulkan::Vulkan)
endif()

if(RAYOL_USE_IMGUI)
    if(NOT RAYOL_ENABLE_VULKAN)
        message(FATAL_ERROR "ImGui backend is configured for Vulkan; enable RAYOL_ENABLE_VULKAN or set RAYOL_USE_IMGUI=OFF.")
    endif()
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
    target_compile_definitions(rayol PRIVATE RAYOL_HAS_IMGUI=1)
else()
    target_compile_definitions(rayol PRIVATE RAYOL_HAS_IMGUI=0)
endif()
