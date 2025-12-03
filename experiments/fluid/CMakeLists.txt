add_library(rayol_fluid STATIC
    fluid_sim.cpp
    raymarch.cpp
    fluid_experiment.cpp
    fluid_renderer.cpp
)

target_include_directories(rayol_fluid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rayol_fluid PUBLIC cxx_std_20)
target_link_libraries(rayol_fluid PUBLIC Vulkan::Vulkan)
if(DEFINED RAYOL_FLUID_SHADER_DIR)
    target_compile_definitions(rayol_fluid PUBLIC RAYOL_FLUID_SHADER_DIR=\"${RAYOL_FLUID_SHADER_DIR}\")
endif()
