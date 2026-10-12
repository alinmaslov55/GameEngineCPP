# Gather source files
set(ENGINE_SOURCES
    src/Core/Application.cpp
    src/Core/Window.cpp
)

# Create the Engine as a static library
add_library(Engine STATIC ${ENGINE_SOURCES})

# Expose the include/ directory to anything that links to this library
target_include_directories(Engine PUBLIC include)

# Link dependencies to the Engine
target_link_libraries(Engine PUBLIC
    SDL3::SDL3
    glm::glm
    imgui::imgui
    box2d::box2d
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
