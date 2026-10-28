# Automatically gather all C++ source files
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(NOT ENGINE_BUILD_EDITOR)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "[\\\\/]src[\\\\/]ImGui[\\\\/]")
endif()

# Create the Engine as a static library
add_library(Engine STATIC ${ENGINE_SOURCES})

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
