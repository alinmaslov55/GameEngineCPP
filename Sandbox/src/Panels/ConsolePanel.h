#ifndef SANDBOX_CONSOLEPANEL_H
#define SANDBOX_CONSOLEPANEL_H

#include <imgui.h>

namespace Sandbox {

class ConsolePanel {
public:
    void OnImGuiRender(bool* pOpen);

private:
    ImGuiTextFilter m_LogFilter;
    bool m_AutoScroll = true;
};

} // namespace Sandbox

#endif // SANDBOX_CONSOLEPANEL_H

