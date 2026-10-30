#include "Panels/ConsolePanel.h"

#include <Engine/Utils/Logger.h>

namespace Sandbox {

void ConsolePanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Console", pOpen);
    {
        if (ImGui::Button("Clear")) {
            Engine::Logger::ClearLogs();
        }
        ImGui::SameLine();

        const char* levelNames[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
        int currentLevel = static_cast<int>(Engine::Logger::GetLevel());
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("Level", &currentLevel, levelNames, IM_ARRAYSIZE(levelNames))) {
            Engine::Logger::SetLevel(static_cast<spdlog::level::level_enum>(currentLevel));
        }
        ImGui::SameLine();

        m_LogFilter.Draw("Filter", 180.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::Separator();

        const uint64_t currentRev = Engine::Logger::GetLogRevision();
        const bool filterChanged = (std::strcmp(m_PreviousFilterInput, m_LogFilter.InputBuf) != 0);

        if (currentRev != m_LastSeenRevision || filterChanged) {
            if (currentRev != m_LastSeenRevision) {
                m_CachedLogs = Engine::Logger::GetRecentLogs();
                m_LastSeenRevision = currentRev;
            }

            if (filterChanged) {
                std::strncpy(m_PreviousFilterInput, m_LogFilter.InputBuf, sizeof(m_PreviousFilterInput) - 1);
                m_PreviousFilterInput[sizeof(m_PreviousFilterInput) - 1] = '\0';
            }

            m_FilteredIndices.clear();
            m_FilteredIndices.reserve(m_CachedLogs.size());
            for (size_t i = 0; i < m_CachedLogs.size(); ++i) {
                if (m_LogFilter.PassFilter(m_CachedLogs[i].Message.c_str())) {
                    m_FilteredIndices.push_back(i);
                }
            }
        }

        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_FilteredIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = m_CachedLogs[m_FilteredIndices[row]];

                ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                switch (entry.Level) {
                    case spdlog::level::trace:    color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break;
                    case spdlog::level::debug:    color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f); break;
                    case spdlog::level::info:     color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); break;
                    case spdlog::level::warn:     color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); break;
                    case spdlog::level::err:      color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f); break;
                    case spdlog::level::critical: color = ImVec4(1.0f, 0.1f, 0.5f, 1.0f); break;
                    default: break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry.Message.c_str());
                ImGui::PopStyleColor();
            }
        }
        clipper.End();

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace Sandbox

