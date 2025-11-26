cmake_minimum_required(VERSION 3.20)
project(rayol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)

add_executable(rayol
    src/rayol.cpp
)

target_link_libraries(rayol PRIVATE SDL3::SDL3)
if(TARGET SDL3::SDL3main)
    target_link_libraries(rayol PRIVATE SDL3::SDL3main)
endif()
