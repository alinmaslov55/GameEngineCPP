# Automatically gather all C++ source files
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(NOT ENGINE_BUILD_EDITOR)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "[\\\\/]src[\\\\/]ImGui[\\\\/]")
endif()

# Create the Engine as a static library
add_library(Engine STATIC ${ENGINE_SOURCES})

# Apply strict compiler warnings
target_compile_options(Engine PRIVATE ${ENGINE_COMPILER_WARNINGS})

# Configure Precompiled Headers
target_precompile_headers(Engine PUBLIC
    <iostream>
    <string>
    <string_view>
    <vector>
    <memory>
    <unordered_map>
    <unordered_set>
    <array>
    <algorithm>
    <utility>
    <cstdint>
    <cmath>
    <filesystem>
    <chrono>
    <functional>
    <glad/glad.h>
    <SDL3/SDL.h>
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <glm/gtc/type_ptr.hpp>
    <nlohmann/json.hpp>
    <spdlog/spdlog.h>
)

# Expose the include/ directory to anything that links to this library
target_include_directories(Engine PUBLIC include)

# Propagate Editor vs Distribution mode to Engine and all downstream targets
if(ENGINE_BUILD_EDITOR)
    target_compile_definitions(Engine PUBLIC ENGINE_BUILD_EDITOR)
else()
    target_compile_definitions(Engine PUBLIC ENGINE_DIST)
endif()

# Link core runtime dependencies to the Engine
target_link_libraries(Engine PUBLIC
    SDL3::SDL3
    glm::glm
    box2d::box2d
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    glad::glad
)

if(ENGINE_BUILD_EDITOR)
    target_link_libraries(Engine PUBLIC imgui::imgui)
endif()
