#ifndef SANDBOX_TILEPALETTEPANEL_H
#define SANDBOX_TILEPALETTEPANEL_H

#include <cstdint>

namespace Sandbox {

class TilePalettePanel {
public:
    void OnImGuiRender(bool* pOpen);

    uint32_t GetSelectedTileID() const { return m_SelectedTileID; }
    bool IsSelectedTileWalkable() const { return m_SelectedTileWalkable; }

private:
    uint32_t m_SelectedTileID = 1; // Default to Grass
    bool m_SelectedTileWalkable = true;
};

} // namespace Sandbox

#endif // SANDBOX_TILEPALETTEPANEL_H

