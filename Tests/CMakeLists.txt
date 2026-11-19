file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

add_executable(EngineTests ${TEST_SOURCES})

# Apply strict compiler warnings
target_compile_options(EngineTests PRIVATE ${ENGINE_COMPILER_WARNINGS})

target_link_libraries(EngineTests PRIVATE
    Engine
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME EngineTests COMMAND EngineTests)
set_tests_properties(EngineTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")


