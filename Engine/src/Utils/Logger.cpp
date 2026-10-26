#include "Engine/Utils/Logger.h"

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

    void Clear() {
        std::lock_guard<std::mutex> lock(this->mutex_);
        m_Entries.clear();
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
    }

    void flush_() override {}

private:
    size_t m_MaxEntries;
    std::deque<Logger::LogEntry> m_Entries;
};

std::shared_ptr<RingBufferLogSink> s_RingBufferSink{nullptr};
spdlog::level::level_enum s_CurrentLevel = spdlog::level::info;
bool s_Initialized = false;
} // namespace

std::shared_ptr<spdlog::logger> Logger::s_CoreLogger{nullptr};
std::shared_ptr<spdlog::logger> Logger::s_ClientLogger{nullptr};

void Logger::Init() {
    if (s_Initialized) {
        return;
    }

    std::filesystem::create_directories("logs");

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/Engine.log", true);
    fileSink->set_pattern("[%Y-%m-%d %T] [%l] %n: %v");

    s_RingBufferSink = std::make_shared<RingBufferLogSink>(1000);
    s_RingBufferSink->set_pattern("[%T] [%l] %n: %v");

    std::vector<spdlog::sink_ptr> sinks = { fileSink, s_RingBufferSink };

    s_CoreLogger = std::make_shared<spdlog::logger>("ENGINE", sinks.begin(), sinks.end());
    s_CoreLogger->set_level(s_CurrentLevel);
    s_CoreLogger->flush_on(spdlog::level::warn);
    spdlog::register_logger(s_CoreLogger);

    s_ClientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());
    s_ClientLogger->set_level(s_CurrentLevel);
    s_ClientLogger->flush_on(spdlog::level::warn);
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

void Logger::SetLevel(spdlog::level::level_enum level) {
    if (!s_Initialized) {
        Init();
    }
    s_CurrentLevel = level;
    if (s_CoreLogger) {
        s_CoreLogger->set_level(level);
    }
    if (s_ClientLogger) {
        s_ClientLogger->set_level(level);
    }
}

spdlog::level::level_enum Logger::GetLevel() {
    return s_CurrentLevel;
}

std::vector<Logger::LogEntry> Logger::GetRecentLogs() {
    if (!s_RingBufferSink) {
        return {};
    }
    return s_RingBufferSink->GetEntries();
}

void Logger::ClearLogs() {
    if (s_RingBufferSink) {
        s_RingBufferSink->Clear();
    }
}

} // namespace Engine
