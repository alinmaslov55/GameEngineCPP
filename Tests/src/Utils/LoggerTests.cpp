#include <gtest/gtest.h>
#include "Engine/Utils/Logger.h"

namespace {

TEST(LoggerTest, LoggerInitializationAndRevisionIncrement) {
    Engine::Logger::Init();

    ASSERT_NE(Engine::Logger::GetCoreLogger(), nullptr);
    ASSERT_NE(Engine::Logger::GetClientLogger(), nullptr);

    uint64_t initialRevision = Engine::Logger::GetLogRevision();

    // Trace / Info / Warn logs should be accepted
    ENGINE_CORE_INFO("Logger test message: 1");
    ENGINE_INFO("Client logger test message: 2");

    uint64_t nextRevision = Engine::Logger::GetLogRevision();
    EXPECT_GE(nextRevision, initialRevision);

    // Retrieve recent logs
    auto logs = Engine::Logger::GetRecentLogs();
    EXPECT_FALSE(logs.empty());
}

} // namespace

