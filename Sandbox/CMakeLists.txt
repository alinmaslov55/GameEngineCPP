# Automatically gather all Sandbox C++ source files
file(GLOB_RECURSE SANDBOX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(ENGINE_BUILD_EDITOR)
    list(FILTER SANDBOX_SOURCES EXCLUDE REGEX "[\\\\/]src[\\\\/]Runtime[\\\\/]")
else()
    list(FILTER SANDBOX_SOURCES EXCLUDE REGEX "[\\\\/]src[\\\\/](Editor|Panels)[\\\\/]")
endif()

# Create the executable
add_executable(EngineSandbox ${SANDBOX_SOURCES})

target_include_directories(EngineSandbox PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Link the Engine library
target_link_libraries(EngineSandbox PRIVATE Engine)

# Standalone player packaging flags when building without the Editor
if(NOT ENGINE_BUILD_EDITOR)
    if(MSVC)
        # Hide the console window in Release/RelWithDebInfo/MinSizeRel while keeping it in Debug for logs
        target_link_options(EngineSandbox PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
        )
    elseif(APPLE)
        set_target_properties(EngineSandbox PROPERTIES MACOSX_BUNDLE TRUE)
    endif()
endif()

# Set working directory to Sandbox/ when launching from Visual Studio
set_target_properties(EngineSandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Copy assets and startup scene next to the executable post-build for standalone portability
add_custom_command(TARGET EngineSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:EngineSandbox>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:EngineSandbox>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Village.scene"
        "${CMAKE_CURRENT_SOURCE_DIR}/test.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/font_atlas.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/jump.wav"
        "$<TARGET_FILE_DIR:EngineSandbox>"
    COMMENT "Copying Sandbox assets to output directory"
)
