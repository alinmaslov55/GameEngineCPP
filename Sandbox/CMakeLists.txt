set(SANDBOX_SOURCES
    src/main.cpp
)

# Create the executable
add_executable(EngineSandbox ${SANDBOX_SOURCES})

# Link the Engine library
target_link_libraries(EngineSandbox PRIVATE Engine)

# Set working directory to Sandbox/ when launching from Visual Studio
set_target_properties(EngineSandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Copy assets next to the executable post-build for standalone portability
add_custom_command(TARGET EngineSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:EngineSandbox>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:EngineSandbox>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/test.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/font_atlas.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/jump.wav"
        "$<TARGET_FILE_DIR:EngineSandbox>"
    COMMENT "Copying Sandbox assets to output directory"
)
