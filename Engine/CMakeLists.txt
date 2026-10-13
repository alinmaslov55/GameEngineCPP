# Gather source files
set(ENGINE_SOURCES
    src/Core/Application.cpp
    src/Core/Window.cpp
    src/Core/Time.cpp

    src/Renderer/Renderer.cpp
    src/Renderer/Buffer.cpp
    src/Renderer/VertexArray.cpp
    src/Renderer/Shader.cpp
    src/Renderer/Texture.cpp
    src/Renderer/stb_image.cpp

    src/Scene/Transform.cpp
    src/Scene/Component.cpp
    src/Scene/Object.cpp
    src/Scene/GameObject.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneManager.cpp
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
    glad::glad
)
