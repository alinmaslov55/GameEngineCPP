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
    m_NeedsRefresh = true;
}

void ContentBrowserPanel::Refresh() {
    m_NeedsRefresh = true;
}

void ContentBrowserPanel::RefreshEntries() {
    m_CachedEntries.clear();
    m_LastScannedDirectory = m_CurrentDirectory;
    m_NeedsRefresh = false;

    if (m_CurrentDirectory.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(m_CurrentDirectory, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory, ec)) {
        CachedAssetEntry item;
        item.Path = entry.path();
        item.Filename = item.Path.filename().string();
        item.FilenameLower = ToLowerASCII(item.Filename);
        item.ItemPath = item.Path.string();
        item.IsDirectory = entry.is_directory();
        item.IsImage = IsImageExtension(item.Path);
        if (item.IsImage) {
            item.Thumbnail = Engine::ResourceManager::Load<Engine::Texture>(item.ItemPath);
        }
        m_CachedEntries.push_back(std::move(item));
    }

    std::sort(m_CachedEntries.begin(), m_CachedEntries.end(), [](const CachedAssetEntry& a, const CachedAssetEntry& b) {
        if (a.IsDirectory != b.IsDirectory) {
            return a.IsDirectory > b.IsDirectory;
        }
        return a.FilenameLower < b.FilenameLower;
    });
}

void ContentBrowserPanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    if (m_NeedsRefresh || m_CurrentDirectory != m_LastScannedDirectory) {
        RefreshEntries();
    }

    ImGui::Begin("Asset Browser", pOpen);

    // 1. Top Navigation, Search Filter & 2.5D Viewport Drop Archetype Bar
    if (m_CurrentDirectory != m_BaseDirectory) {
        if (ImGui::Button("<- Back")) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
            m_NeedsRefresh = true;
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
        ENGINE_DEBUG("Asset Browser: Drop archetype changed to '{}' ({})", archetypeLabels[archIdx], archIdx);
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

    // 2. Render cached directories first, then files matching optional search filter
    if (!m_CachedEntries.empty()) {
        const std::string filterLower = ToLowerASCII(m_SearchFilter);

        const float cellPadding = 14.0f;
        const float cellSize = m_ThumbnailSize + cellPadding;
        const float panelWidth = ImGui::GetContentRegionAvail().x;
        const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        ImGui::Columns(columnCount, nullptr, false);

        for (const auto& entry : m_CachedEntries) {
            if (!filterLower.empty() && entry.FilenameLower.find(filterLower) == std::string::npos) {
                continue;
            }

            ImGui::PushID(entry.Filename.c_str());

            if (entry.IsDirectory) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.23f, 0.31f, 0.90f));
                if (ImGui::Button("[DIR]", ImVec2(m_ThumbnailSize, m_ThumbnailSize))) {
                    m_CurrentDirectory /= entry.Path.filename();
                    m_NeedsRefresh = true;
                    ENGINE_DEBUG("Asset Browser: Opened directory '{}'", m_CurrentDirectory.string());
                }
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s", entry.Filename.c_str());
            } else if (entry.IsImage) {
                const auto& tex = entry.Thumbnail;
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
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", entry.ItemPath.c_str(), entry.ItemPath.size() + 1);
                    if (texID != 0) {
                        ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(texID)),
                                     ImVec2(48.0f, 48.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                        ImGui::SameLine();
                    }
                    ImGui::Text("Drop '%s'\ninto Scene or Inspector", entry.Filename.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::IsItemHovered() && tex) {
                    ImGui::SetTooltip("%s (%dx%d)\nRight-click: 2.5D Cutout Processor\nDrag into 3D Scene Viewport to spawn",
                                      entry.Filename.c_str(), tex->GetWidth(), tex->GetHeight());
                }

                // Right-click In-Engine 2.5D Art Pipeline Context Menu
                if (ImGui::BeginPopupContextItem("ImageCutoutContextMenu")) {
                    ImGui::TextDisabled("2.5D Cutout Art Pipeline");
                    ImGui::Separator();

                    if (ImGui::MenuItem("Process Cutout (Auto Key + Bottom-Flush + Bleed)")) {
                        std::filesystem::path outPath = entry.Path;
                        outPath.replace_extension(".png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        ENGINE_INFO("Asset Browser: Processing cutout for '{}'...", entry.Path.string());
                        const auto res = Engine::CutoutProcessor::ProcessFile(entry.Path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_NeedsRefresh = true;
                            m_LastStatusMessage = "Processed " + outPath.filename().string() +
                                                  " (" + std::to_string(res.OutputWidth) + "x" +
                                                  std::to_string(res.OutputHeight) + ")";
                            ENGINE_INFO("Asset Browser: Cutout processed successfully: '{}' ({}x{}, aspect {:.3f})",
                                        outPath.string(), res.OutputWidth, res.OutputHeight, res.SuggestedAspectRatio);
                        } else {
                            m_LastStatusMessage = "Cutout failed: " + res.ErrorMessage;
                            ENGINE_ERROR("Asset Browser: Cutout processing failed for '{}': {}", entry.Path.string(), res.ErrorMessage);
                        }
                    }

                    if (ImGui::MenuItem("Save Processed Copy (<name>_cutout.png)")) {
                        const std::filesystem::path outPath =
                            entry.Path.parent_path() / (entry.Path.stem().string() + "_cutout.png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        ENGINE_INFO("Asset Browser: Saving processed cutout copy for '{}' to '{}'...", entry.Path.string(), outPath.string());
                        const auto res = Engine::CutoutProcessor::ProcessFile(entry.Path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_NeedsRefresh = true;
                            m_LastStatusMessage = "Saved " + outPath.filename().string();
                            ENGINE_INFO("Asset Browser: Cutout copy saved successfully: '{}' ({}x{})",
                                        outPath.string(), res.OutputWidth, res.OutputHeight);
                        } else {
                            m_LastStatusMessage = "Cutout failed: " + res.ErrorMessage;
                            ENGINE_ERROR("Asset Browser: Cutout copy failed for '{}': {}", entry.Path.string(), res.ErrorMessage);
                        }
                    }

                    if (ImGui::MenuItem("Bottom-Flush & 4px Alpha Bleed Only (Keep Alpha)")) {
                        std::filesystem::path outPath = entry.Path;
                        outPath.replace_extension(".png");
                        Engine::CutoutProcessSettings settings;
                        settings.BackgroundMode = Engine::CutoutKeyMode::PreserveExistingAlpha;
                        settings.AutoTrimAndBottomFlush = true;
                        settings.AlphaBleedPasses = 4;

                        ENGINE_INFO("Asset Browser: Aligning & bleeding '{}' (preserving alpha)...", entry.Path.string());
                        const auto res = Engine::CutoutProcessor::ProcessFile(entry.Path, outPath, settings);
                        if (res.Success) {
                            Engine::ResourceManager::Reload<Engine::Texture>(outPath.string());
                            m_NeedsRefresh = true;
                            m_LastStatusMessage = "Aligned & bled " + outPath.filename().string();
                            ENGINE_INFO("Asset Browser: Align & bleed completed successfully: '{}' ({}x{})",
                                        outPath.string(), res.OutputWidth, res.OutputHeight);
                        } else {
                            m_LastStatusMessage = "Process failed: " + res.ErrorMessage;
                            ENGINE_ERROR("Asset Browser: Align & bleed failed for '{}': {}", entry.Path.string(), res.ErrorMessage);
                        }
                    }

                    ImGui::EndPopup();
                }

                ImGui::TextWrapped("%s", entry.Filename.c_str());
            } else {
                ImGui::Button("FILE", ImVec2(m_ThumbnailSize, m_ThumbnailSize));
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", entry.ItemPath.c_str(), entry.ItemPath.size() + 1);
                    ImGui::Text("Drag %s", entry.Filename.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::TextWrapped("%s", entry.Filename.c_str());
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
    }

    ImGui::End();
}

} // namespace Sandbox
