set(SANDBOX_SOURCES
    src/main.cpp
)

# Create the executable
add_executable(EngineSandbox ${SANDBOX_SOURCES})

# Link the Engine library
target_link_libraries(EngineSandbox PRIVATE Engine)
