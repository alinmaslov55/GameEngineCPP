#ifndef SANDBOX_CONTENTBROWSERPANEL_H
#define SANDBOX_CONTENTBROWSERPANEL_H

#include <filesystem>

namespace Sandbox {

class ContentBrowserPanel {
public:
    void SetRootDirectory(const std::filesystem::path& rootDirectory);
    void OnImGuiRender(bool* pOpen);

private:
    std::filesystem::path m_BaseDirectory;
    std::filesystem::path m_CurrentDirectory;
};

} // namespace Sandbox

#endif // SANDBOX_CONTENTBROWSERPANEL_H

