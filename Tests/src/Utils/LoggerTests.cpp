#include <gtest/gtest.h>
#include "Engine/Utils/Logger.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

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

TEST(LoggerTest, RepeatedInitShutdownAndConcurrentLoggingAreSafe) {
    for (int cycle = 0; cycle < 3; ++cycle) {
        Engine::Logger::Shutdown();
        Engine::Logger::Init();
        Engine::Logger::SetLevel(spdlog::level::debug);
        EXPECT_EQ(Engine::Logger::GetLevel(), spdlog::level::debug);
        ASSERT_NE(Engine::Logger::GetCoreLogger(), nullptr);
        ASSERT_NE(Engine::Logger::GetClientLogger(), nullptr);
        ENGINE_CORE_INFO("Logger restart cycle {}", cycle);
    }

    constexpr int kThreads = 6;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([t]() {
            for (int i = 0; i < 40; ++i) {
                Engine::Logger::Init();
                auto core = Engine::Logger::GetCoreLogger();
                auto client = Engine::Logger::GetClientLogger();
                EXPECT_NE(core, nullptr);
                EXPECT_NE(client, nullptr);
                ENGINE_CORE_INFO("Concurrent log thread={} iter={}", t, i);
                if ((i % 10) == 0) {
                    (void)Engine::Logger::GetRecentLogs();
                    (void)Engine::Logger::GetLogRevision();
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    Engine::Logger::SetLevel(spdlog::level::info);
    Engine::Logger::ClearLogs();
}

TEST(LoggerTest, PostShutdownLoggingPreservesSessionLog) {
    Engine::Logger::Init();
    const std::string preMessage = "SENTINEL_PRE_SHUTDOWN_LOG_ENTRY";
    ENGINE_CORE_INFO("{}", preMessage);
    Engine::Logger::Shutdown();

    const std::string postMessage = "SENTINEL_POST_SHUTDOWN_LOG_ENTRY";
    ENGINE_CORE_INFO("{}", postMessage);
    Engine::Logger::GetCoreLogger()->flush();

    // Read logs/Engine.log and verify both messages exist (append mode instead of truncate)
    std::ifstream file("logs/Engine.log");
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    EXPECT_NE(content.find(preMessage), std::string::npos);
    EXPECT_NE(content.find(postMessage), std::string::npos);
}

} // namespace

