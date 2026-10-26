#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include <memory>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Engine {

class Logger {
public:
    struct LogEntry {
        spdlog::level::level_enum Level = spdlog::level::info;
        std::string Message;
    };

    static void Init();
    static void Shutdown();

    static void SetLevel(spdlog::level::level_enum level);
    static spdlog::level::level_enum GetLevel();

    static std::vector<LogEntry> GetRecentLogs();
    static void ClearLogs();

    static std::shared_ptr<spdlog::logger>& GetCoreLogger() {
        if (!s_CoreLogger) Init();
        return s_CoreLogger;
    }

    static std::shared_ptr<spdlog::logger>& GetClientLogger() {
        if (!s_ClientLogger) Init();
        return s_ClientLogger;
    }

    template<typename... Args>
    static void Trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        GetClientLogger()->critical(fmt, std::forward<Args>(args)...);
    }

private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
};

} // namespace Engine

// Core Engine & Client Logging Macros (Strip TRACE in Release builds)
#ifdef NDEBUG
    #define ENGINE_CORE_TRACE(...)    ((void)0)
    #define ENGINE_TRACE(...)         ((void)0)
#else
    #define ENGINE_CORE_TRACE(...)    ::Engine::Logger::GetCoreLogger()->trace(__VA_ARGS__)
    #define ENGINE_TRACE(...)         ::Engine::Logger::GetClientLogger()->trace(__VA_ARGS__)
#endif

#define ENGINE_CORE_DEBUG(...)    ::Engine::Logger::GetCoreLogger()->debug(__VA_ARGS__)
#define ENGINE_CORE_INFO(...)     ::Engine::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define ENGINE_CORE_WARN(...)     ::Engine::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define ENGINE_CORE_ERROR(...)    ::Engine::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define ENGINE_CORE_CRITICAL(...) ::Engine::Logger::GetCoreLogger()->critical(__VA_ARGS__)

#define ENGINE_DEBUG(...)         ::Engine::Logger::GetClientLogger()->debug(__VA_ARGS__)
#define ENGINE_INFO(...)          ::Engine::Logger::GetClientLogger()->info(__VA_ARGS__)
#define ENGINE_WARN(...)          ::Engine::Logger::GetClientLogger()->warn(__VA_ARGS__)
#define ENGINE_ERROR(...)         ::Engine::Logger::GetClientLogger()->error(__VA_ARGS__)
#define ENGINE_CRITICAL(...)      ::Engine::Logger::GetClientLogger()->critical(__VA_ARGS__)

#endif // UTILS_LOGGER_H