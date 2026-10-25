#ifndef SCENE_TILEMAP_H
#define SCENE_TILEMAP_H
#include "Engine/Scene/Component.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace Engine {
struct Tile {
    uint32_t id = 0;
    bool walkable = true;
};

class TileSet {
public:
    void AddTile(uint32_t id, const std::shared_ptr<SubTexture2D>& texture) {
        ENGINE_CORE_DEBUG("TileSet: Added tile with ID {}", id);
        m_Tiles[id] = texture;
    }
    
    std::shared_ptr<SubTexture2D> GetTileTexture(uint32_t id) {
        return m_Tiles.count(id) ? m_Tiles[id] : nullptr;
    }
private:
    std::unordered_map<uint32_t, std::shared_ptr<SubTexture2D>> m_Tiles;
};

class Tilemap : public Component {
public:
    Tilemap(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {
        ENGINE_CORE_INFO("Tilemap created with dimensions: {}x{} ({} total tiles)", width, height, width * height);
        m_Grid.resize(width * height);
    }

    void SetTile(uint32_t x, uint32_t y, uint32_t tileId) {
        if (x < m_Width && y < m_Height) {
            m_Grid[y * m_Width + x].id = tileId;
        } else {
            ENGINE_CORE_WARN("Tilemap::SetTile: Coordinates ({}, {}) out of bounds ({}x{})", x, y, m_Width, m_Height);
        }
    }

    const Tile& GetTile(uint32_t x, uint32_t y) const {
        return m_Grid[y * m_Width + x];
    }

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    std::shared_ptr<TileSet> tileSet = std::make_shared<TileSet>();
private:
    uint32_t m_Width, m_Height;
    std::vector<Tile> m_Grid;
};
} // namespace Engine

#endif // SCENE_TILEMAP_H