add_executable(EngineTests
    src/EngineTests.cpp
)

target_link_libraries(EngineTests PRIVATE
    Engine
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME EngineTests COMMAND EngineTests)

