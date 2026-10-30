#ifndef SANDBOX_CONSOLEPANEL_H
#define SANDBOX_CONSOLEPANEL_H

#include <imgui.h>
#include <Engine/Utils/Logger.h>
#include <vector>
#include <cstdint>

namespace Sandbox {

class ConsolePanel {
public:
    void OnImGuiRender(bool* pOpen);

private:
    ImGuiTextFilter m_LogFilter;
    bool m_AutoScroll = true;

    std::vector<Engine::Logger::LogEntry> m_CachedLogs;
    std::vector<size_t> m_FilteredIndices;
    uint64_t m_LastSeenRevision = 0;
    char m_PreviousFilterInput[256] = "";
};

} // namespace Sandbox

#endif // SANDBOX_CONSOLEPANEL_H

