#include "Panels/ContentBrowserPanel.h"

#include <Engine/Core/ResourceManager.h>
#include <Engine/Renderer/Texture.h>
#include <Engine/Utils/CutoutProcessor.h>
#include <Engine/Utils/Logger.h>

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace Sandbox {

namespace {

std::string ToLowerASCII(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool IsImageExtension(const std::filesystem::path& path) {
    const std::string ext = ToLowerASCII(path.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

} // namespace

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

    // 1. Top Navigation, Search Filter & 2.5D Viewport Drop Archetype Bar
    if (m_CurrentDirectory != m_BaseDirectory) {
        if (ImGui::Button("<- Back")) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ENGINE_DEBUG("Asset Browser: Navigated back to '{}'", m_CurrentDirectory.string());
        }
        ImGui::SameLine();
    }

    std::error_code relEc;
    const auto relDir = std::filesystem::relative(m_CurrentDirectory, m_BaseDirectory, relEc);
    const std::string breadcrumb = (!relEc && relDir != ".")
        ? ("assets/" + relDir.generic_string())
        : "assets";
    ImGui::TextDisabled("%s", breadcrumb.c_str());

    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##AssetSearch", "Filter assets...", m_SearchFilter, sizeof(m_SearchFilter));

    ImGui::SameLine();
    const char* archetypeLabels[] = {
        "Drop: Auto-Detect",
        "Drop: Swaying Foliage",
        "Drop: Pine / Tree",
        "Drop: Ritual Banner",
        "Drop: Solid Standee",
        "Drop: Flat Ground Decal"
    };
    int archIdx = static_cast<int>(m_DropArchetype);
    ImGui::SetNextItemWidth(175.0f);
    if (ImGui::Combo("##DropArchetype", &archIdx, archetypeLabels, 6)) {
        m_DropArchetype = static_cast<DioramaDropArchetype>(archIdx);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Selects how textures dragged onto the 3D Scene Viewport are configured.");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(95.0f);
    ImGui::SliderFloat("##ThumbSize", &m_ThumbnailSize, 52.0f, 128.0f, "%.0f px");

    if (!m_LastStatusMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.62f, 1.0f), "%s", m_LastStatusMessage.c_str());
    }

    ImGui::Separator();

    // 2. Gather & sort directories first, then files matching optional search filter
    if (!m_CurrentDirectory.empty() && std::filesystem::exists(m_CurrentDirectory)) {
        std::vector<std::filesystem::directory_entry> entries;
        const std::string filterLower = ToLowerASCII(m_SearchFilter);

        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const std::string filename = entry.path().filename().string();
            if (!filterLower.empty() && ToLowerASCII(filename).find(filterLower) == std::string::npos) {
                continue;
            }
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) {
                return a.is_directory() > b.is_directory();
            }
            return a.path().filename().string() < b.path().filename().string();
        });

        const float cellPadding = 14.0f;
        const float cellSize = m_ThumbnailSize + cellPadding;
        const float panelWidth = ImGui::GetContentRegionAvail().x;
        const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        ImGui::Columns(columnCount, nullptr, false);

        for (const auto& directoryEntry : entries) {
            const auto& path = directoryEntry.path();
            const std::string filenameString = path.filename().string();
            const std::string itemPath = path.string();

            ImGui::PushID(filenameString.c_str());

            if (directoryEntry.is_directory()) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.23f, 0.31f, 0.90f));
                if (ImGui::Button("[DIR]", ImVec2(m_ThumbnailSize, m_ThumbnailSize))) {
                    m_CurrentDirectory /= path.filename();
                    ENGINE_DEBUG("Asset Browser: Opened directory '{}'", m_CurrentDirectory.string());
                }
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s", filenameString.c_str());
            } else if (IsImageExtension(path)) {
                auto tex = Engine::ResourceManager::Load<Engine::Texture>(itemPath);
                const uint32_t texID = tex ? tex->GetRendererID() : 0;

                if (texID != 0) {
                    // OpenGL textures are flipped bottom-up on load; UV (0,1)->(1,0) displays upright in ImGui
                    ImGui::ImageButton(
                        "##thumb",
                        reinterpret_cast<void*>(static_cast<uintptr_t>(texID)),
                        ImVec2(m_ThumbnailSize, m_ThumbnailSize),
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f)
                    );
                } else {
                    ImGui::Button("IMG", ImVec2(m_ThumbnailSize, m_ThumbnailSize));
                }

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                    if (texID != 0) {
                        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(texID)),
                                     ImVec2(48.0f, 48.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                        ImGui::SameLine();
                    }
                    ImGui::Text("Drop '%s'\ninto Scene or Inspector", filenameString.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::IsItemHovered() && tex) {
                    ImGui::SetTooltip("%s (%dx%d)\nRight-click: 2.5D Cutout Processor\nDrag into 3D Scene Viewport to spawn",
                                      filenameString.c_str(), tex->GetWidth(), tex->GetHeight());
                }

                // Right-click In-Engine 2.5D Art Pipeline Context Menu
                if (ImGui::BeginPopupContextItem("ImageCutoutContextMenu")) {
                    ImGui::TextDisabled("2.5D Cutout Art Pipeline");
                    ImGui::Separator();

                    if (ImGui::MenuItem("Process Cutout (Auto Key + Bottom-Flush + Bleed)")) {
                        std::filesystem::path outPath = path;
                        outPath.replace_extension(".png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        const auto res = Engine::CutoutProcessor::ProcessFile(path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_LastStatusMessage = "Processed " + outPath.filename().string() +
                                                  " (" + std::to_string(res.OutputWidth) + "x" +
                                                  std::to_string(res.OutputHeight) + ")";
                        } else {
                            m_LastStatusMessage = "Cutout failed: " + res.ErrorMessage;
                        }
                    }

                    if (ImGui::MenuItem("Save Processed Copy (<name>_cutout.png)")) {
                        const std::filesystem::path outPath =
                            path.parent_path() / (path.stem().string() + "_cutout.png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        const auto res = Engine::CutoutProcessor::ProcessFile(path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_LastStatusMessage = "Saved " + outPath.filename().string();
                        } else {
                            m_LastStatusMessage = "Cutout failed: " + res.ErrorMessage;
                        }
                    }

                    if (ImGui::MenuItem("Bottom-Flush & 4px Alpha Bleed Only (Keep Alpha)")) {
                        std::filesystem::path outPath = path;
                        outPath.replace_extension(".png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::PreserveExistingAlpha;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        const auto res = Engine::CutoutProcessor::ProcessFile(path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_LastStatusMessage = "Aligned & bled " + outPath.filename().string();
                        } else {
                            m_LastStatusMessage = "Process failed: " + res.ErrorMessage;
                        }
                    }

                    ImGui::EndPopup();
                }

                ImGui::TextWrapped("%s", filenameString.c_str());
            } else {
                ImGui::Button("FILE", ImVec2(m_ThumbnailSize, m_ThumbnailSize));
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), itemPath.size() + 1);
                    ImGui::Text("Drag %s", filenameString.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::TextWrapped("%s", filenameString.c_str());
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    ImGui::End();
}

} // namespace Sandbox
