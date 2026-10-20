#include "Engine/Utils/Logger.h"

#include <filesystem>
#include <spdlog/sinks/basic_file_sink.h>

namespace Engine {

std::shared_ptr<spdlog::logger> Logger::s_CoreLogger{nullptr};
std::shared_ptr<spdlog::logger> Logger::s_ClientLogger{nullptr};
static bool s_Initialized = false;

void Logger::Init() {
    if (s_Initialized) {
        return;
    }

    std::filesystem::create_directories("logs");

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/Engine.log", true);
    fileSink->set_pattern("[%Y-%m-%d %T] [%l] %n: %v");

    s_CoreLogger = std::make_shared<spdlog::logger>("ENGINE", fileSink);
    s_CoreLogger->set_level(spdlog::level::trace);
    s_CoreLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_CoreLogger);

    s_ClientLogger = std::make_shared<spdlog::logger>("APP", fileSink);
    s_ClientLogger->set_level(spdlog::level::trace);
    s_ClientLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_ClientLogger);

    s_Initialized = true;
}

void Logger::Shutdown() {
    if (s_CoreLogger) {
        s_CoreLogger->flush();
    }
    if (s_ClientLogger) {
        s_ClientLogger->flush();
    }
}

} // namespace Engine
