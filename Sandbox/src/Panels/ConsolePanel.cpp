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

        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        const auto logs = Engine::Logger::GetRecentLogs();
        for (const auto& entry : logs) {
            if (!m_LogFilter.PassFilter(entry.Message.c_str())) {
                continue;
            }

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

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace Sandbox

