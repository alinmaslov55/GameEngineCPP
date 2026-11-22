#include "Engine/Utils/Logger.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Engine {

namespace {
class RingBufferLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit RingBufferLogSink(size_t maxEntries = 1000)
        : m_MaxEntries(maxEntries) {}

    std::vector<Logger::LogEntry> GetEntries() {
        std::lock_guard<std::mutex> lock(this->mutex_);
        return { m_Entries.begin(), m_Entries.end() };
    }

    uint64_t GetRevision() const {
        return m_Revision.load(std::memory_order_relaxed);
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(this->mutex_);
        m_Entries.clear();
        m_Revision.fetch_add(1, std::memory_order_relaxed);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        if (m_Entries.size() >= m_MaxEntries) {
            m_Entries.pop_front();
        }

        std::string text = fmt::to_string(formatted);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }

        m_Entries.push_back({ msg.level, std::move(text) });
        m_Revision.fetch_add(1, std::memory_order_relaxed);
    }

    void flush_() override {}

private:
    size_t m_MaxEntries;
    std::deque<Logger::LogEntry> m_Entries;
    std::atomic<uint64_t> m_Revision{ 1 };
};

std::shared_ptr<RingBufferLogSink> s_RingBufferSink{nullptr};
spdlog::level::level_enum s_CurrentLevel = spdlog::level::info;
bool s_Initialized = false;
bool s_HasInitializedOnce = false;
std::mutex s_LoggerMutex;

void InitLocked(std::shared_ptr<spdlog::logger>& coreLogger,
                std::shared_ptr<spdlog::logger>& clientLogger);
} // namespace

std::shared_ptr<spdlog::logger> Logger::s_CoreLogger{nullptr};
std::shared_ptr<spdlog::logger> Logger::s_ClientLogger{nullptr};

namespace {
void InitLocked(std::shared_ptr<spdlog::logger>& coreLogger,
                std::shared_ptr<spdlog::logger>& clientLogger) {
    if (s_Initialized && coreLogger && clientLogger) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories("logs", ec);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.reserve(2);

    const bool truncate = !s_HasInitializedOnce;
    s_HasInitializedOnce = true;

    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/Engine.log", truncate);
        fileSink->set_pattern("[%Y-%m-%d %T] [%l] %n: %v");
        sinks.push_back(fileSink);
    } catch (const std::exception&) {
        // Fall back gracefully if logs directory or file cannot be opened
    }

    s_RingBufferSink = std::make_shared<RingBufferLogSink>(1000);
    s_RingBufferSink->set_pattern("[%T] [%l] %n: %v");
    sinks.push_back(s_RingBufferSink);

    spdlog::drop("ENGINE");
    spdlog::drop("APP");

    coreLogger = std::make_shared<spdlog::logger>("ENGINE", sinks.begin(), sinks.end());
    coreLogger->set_level(s_CurrentLevel);
    coreLogger->flush_on(spdlog::level::warn);
    spdlog::register_logger(coreLogger);

    clientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());
    clientLogger->set_level(s_CurrentLevel);
    clientLogger->flush_on(spdlog::level::warn);
    spdlog::register_logger(clientLogger);

    s_Initialized = true;
}
} // namespace

void Logger::Init() {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    InitLocked(s_CoreLogger, s_ClientLogger);
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    if (s_CoreLogger) {
        s_CoreLogger->flush();
    }
    if (s_ClientLogger) {
        s_ClientLogger->flush();
    }
    spdlog::drop("ENGINE");
    spdlog::drop("APP");
    s_CoreLogger.reset();
    s_ClientLogger.reset();
    s_RingBufferSink.reset();
    s_Initialized = false;
}

void Logger::SetLevel(spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    InitLocked(s_CoreLogger, s_ClientLogger);
    s_CurrentLevel = level;
    if (s_CoreLogger) {
        s_CoreLogger->set_level(level);
    }
    if (s_ClientLogger) {
        s_ClientLogger->set_level(level);
    }
}

spdlog::level::level_enum Logger::GetLevel() {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    return s_CurrentLevel;
}

std::shared_ptr<spdlog::logger> Logger::GetCoreLogger() {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    if (!s_CoreLogger) {
        InitLocked(s_CoreLogger, s_ClientLogger);
    }
    return s_CoreLogger;
}

std::shared_ptr<spdlog::logger> Logger::GetClientLogger() {
    std::lock_guard<std::mutex> lock(s_LoggerMutex);
    if (!s_ClientLogger) {
        InitLocked(s_CoreLogger, s_ClientLogger);
    }
    return s_ClientLogger;
}

std::vector<Logger::LogEntry> Logger::GetRecentLogs() {
    std::shared_ptr<RingBufferLogSink> sink;
    {
        std::lock_guard<std::mutex> lock(s_LoggerMutex);
        sink = s_RingBufferSink;
    }
    if (!sink) {
        return {};
    }
    return sink->GetEntries();
}

uint64_t Logger::GetLogRevision() {
    std::shared_ptr<RingBufferLogSink> sink;
    {
        std::lock_guard<std::mutex> lock(s_LoggerMutex);
        sink = s_RingBufferSink;
    }
    return sink ? sink->GetRevision() : 0;
}

void Logger::ClearLogs() {
    std::shared_ptr<RingBufferLogSink> sink;
    {
        std::lock_guard<std::mutex> lock(s_LoggerMutex);
        sink = s_RingBufferSink;
    }
    if (sink) {
        sink->Clear();
    }
}

} // namespace Engine
