#include "Panels/ProfilerPanel.h"

#include <Engine/Renderer/Renderer2D.h>
#include <imgui.h>

namespace Sandbox {

void ProfilerPanel::OnImGuiRender(bool* pOpen) {
    if (pOpen && !*pOpen) {
        return;
    }

    ImGui::Begin("Profiler", pOpen);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
    ImGui::Separator();

    const auto stats = Engine::Renderer2D::GetStats();
    ImGui::Text("Renderer2D Stats:");
    ImGui::Text("  Draw Calls: %u", stats.DrawCalls);
    ImGui::Text("  Quads:      %u", stats.QuadCount);
    ImGui::Text("  Vertices:   %u", stats.GetTotalVertexCount());
    ImGui::Text("  Indices:    %u", stats.GetTotalIndexCount());
    ImGui::End();
}

} // namespace Sandbox

