cmake_minimum_required(VERSION 3.20)
project(IsometricEngine VERSION 0.1.0 LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable Link-Time Optimization (IPO/LTO) for Release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# SIMD Intrinsics and Math optimizations
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/arch:AVX2>)
endif()
add_compile_definitions(GLM_FORCE_INTRINSICS)

# Build Configuration Options
option(ENGINE_BUILD_EDITOR "Build the ImGui Editor UI" ON)
option(ENGINE_BUILD_TESTS "Build the GTest unit test suite" ON)

# Finding Installed Packages
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
if(ENGINE_BUILD_EDITOR)
    find_package(imgui CONFIG REQUIRED)
endif()
find_package(box2d CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
if(ENGINE_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
endif()

# Sub-Projects
add_subdirectory(Engine)
add_subdirectory(Sandbox)
if(ENGINE_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

