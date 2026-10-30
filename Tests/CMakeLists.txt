file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

add_executable(EngineTests ${TEST_SOURCES})

target_link_libraries(EngineTests PRIVATE
    Engine
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME EngineTests COMMAND EngineTests)

