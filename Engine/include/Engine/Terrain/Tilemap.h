#ifndef SCENE_TILEMAP_H
#define SCENE_TILEMAP_H
#include "Engine/Scene/Component.h"
#include "Engine/Physics/PhysicsHandles.h"
#include "Engine/Renderer/SubTexture2D.h"
#include "Engine/Utils/Logger.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <limits>

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
    
    const std::shared_ptr<SubTexture2D>& GetTileTexture(uint32_t id) const {
        static const std::shared_ptr<SubTexture2D> s_NullTexture = nullptr;
        auto it = m_Tiles.find(id);
        if (it != m_Tiles.end()) {
            return it->second;
        }
        return s_NullTexture;
    }

    const std::unordered_map<uint32_t, std::shared_ptr<SubTexture2D>>& GetTiles() const { return m_Tiles; }
private:
    std::unordered_map<uint32_t, std::shared_ptr<SubTexture2D>> m_Tiles;
};

class Tilemap final : public Component {
public:
    Tilemap(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {
        const uint64_t totalTiles = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        if (totalTiles > static_cast<uint64_t>(m_Grid.max_size())) {
            ENGINE_CORE_ERROR("Tilemap dimensions {}x{} exceed maximum grid capacity", width, height);
            m_Width = 0;
            m_Height = 0;
            return;
        }
        ENGINE_CORE_INFO("Tilemap created with dimensions: {}x{} ({} total tiles)", width, height, totalTiles);
        m_Grid.resize(static_cast<size_t>(totalTiles));
    }

    void SetTile(uint32_t x, uint32_t y, uint32_t tileId, bool walkable = true) {
        if (x < m_Width && y < m_Height) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_Width) + static_cast<size_t>(x);
            if (idx < m_Grid.size()) {
                auto& tile = m_Grid[idx];
                tile.id = tileId;
                tile.walkable = walkable;
            }
        } else {
            ENGINE_CORE_WARN("Tilemap::SetTile: Coordinates ({}, {}) out of bounds ({}x{})", x, y, m_Width, m_Height);
        }
    }

    const Tile& GetTile(uint32_t x, uint32_t y) const {
        if (x < m_Width && y < m_Height && !m_Grid.empty()) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_Width) + static_cast<size_t>(x);
            if (idx < m_Grid.size()) {
                return m_Grid[idx];
            }
        }
        static const Tile s_InvalidTile{ 0, false };
        return s_InvalidTile;
    }

    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    // Type-safe handle to internal Box2D static body during runtime
    BodyHandle RuntimeBody;

    std::shared_ptr<TileSet> tileSet = std::make_shared<TileSet>();
private:
    uint32_t m_Width, m_Height;
    std::vector<Tile> m_Grid;
};
} // namespace Engine

#endif // SCENE_TILEMAP_H
