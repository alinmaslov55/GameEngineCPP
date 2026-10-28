#include "Panels/ContentBrowserPanel.h"

#include <Engine/Utils/Logger.h>
#include <imgui.h>
#include <string>

namespace Sandbox {

void ContentBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory) {
    m_BaseDirectory = rootDirectory;
    if (!std::filesystem::exists(m_BaseDirectory)) {
        ENGINE_INFO("Creating missing asset directory: '{}'", m_BaseDirectory.string());
        std::filesystem::create_directories(m_BaseDirectory);
    }
    m_CurrentDirectory = m_BaseDirectory;
}

void ContentBrowserPanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Asset Browser", pOpen);

    if (m_CurrentDirectory != m_BaseDirectory) {
        if (ImGui::Button("<- Back")) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ENGINE_DEBUG("Asset Browser: Navigated back to '{}'", m_CurrentDirectory.string());
        }
        ImGui::Separator();
    }

    if (!m_CurrentDirectory.empty() && std::filesystem::exists(m_CurrentDirectory)) {
        for (const auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const auto& path = directoryEntry.path();
            const std::string filenameString = path.filename().string();

            if (directoryEntry.is_directory()) {
                if (ImGui::Button(filenameString.c_str())) {
                    m_CurrentDirectory /= path.filename();
                    ENGINE_DEBUG("Asset Browser: Opened directory '{}'", m_CurrentDirectory.string());
                }
            } else {
                ImGui::Selectable(filenameString.c_str());

                if (ImGui::BeginDragDropSource()) {
                    const std::string itemPath = path.string();
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                    ImGui::Text("Drag %s", filenameString.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }
    }

    ImGui::End();
}

} // namespace Sandbox

