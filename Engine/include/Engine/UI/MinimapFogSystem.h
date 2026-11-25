#ifndef UI_MINIMAPFOGSYSTEM_H
#define UI_MINIMAPFOGSYSTEM_H

#include "Engine/Renderer/Texture.h"
#include "Engine/Terrain/WorldBiomeGenerator.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class Scene;

enum class POIType : uint8_t {
    Player = 0,
    IgnitedBrazier = 1,
    DormantBrazier = 2,
    UmbralShrine   = 3,
    FlarePeg       = 4,
    Custom         = 5
};

struct MinimapPOIPin {
    uint64_t EntityUUID = 0;
    POIType Type = POIType::Custom;
    glm::vec2 WorldXZ{ 0.0f };
    std::string Label;
    glm::vec4 IconColor{ 1.0f };
    bool Discovered = false;
    bool IsClampedToRim = false;
    glm::vec2 MinimapLocalPos{ 0.0f }; // Normalized [-1.0 .. 1.0] relative to circular minimap center
};

class MinimapFogSystem {
public:
    static constexpr uint32_t TextureSize = 256;
    static constexpr float TexelWorldSize = 1.5f; // 1 texel = 1.5m
    static constexpr float WorldDiameter = static_cast<float>(TextureSize) * TexelWorldSize; // 384.0m
    static constexpr float WorldRadius = WorldDiameter * 0.5f;                               // 192.0m

    // RGBA channel constants
    static constexpr uint8_t kDiscoveredAlphaByte = 89; // 0.35 * 255 = 89 (dim exploration fog)

    struct ToroidalCell {
        uint8_t BiomeColorID = 0;
        uint8_t Discovered = 0; // 0 = undiscovered, 89 = discovered (0.35 dim fog)
        uint8_t LiveLit = 0;    // 0..255 live illumination
    };

    MinimapFogSystem();
    ~MinimapFogSystem() = default;

    void Reset();

    // Toroidal scrolling update centered around viewer's world position
    void Update(const glm::vec2& viewerWorldXZ,
                float heroTorchRadius,
                const std::vector<glm::vec3>& lightSources = {},
                const WorldBiomeGenerator* biomeGen = nullptr);

    // Live update querying Scene illumination & registered POIs
    void UpdateFromScene(const Scene& scene, const glm::vec2& viewerWorldXZ);

    // Stamping helpers for live lights & persistent discovery
    void StampExploration(const glm::vec2& worldXZ, float radiusMeters, bool isLiveLit);
    void SetBiomeAtWorld(const glm::vec2& worldXZ, BiomeType biome);

    [[nodiscard]] glm::u8vec4 GetTexelAtWorld(const glm::vec2& worldXZ) const noexcept;
    [[nodiscard]] glm::u8vec4 GetTexelAtGrid(int gx, int gz) const noexcept;
    [[nodiscard]] bool IsDiscovered(const glm::vec2& worldXZ) const noexcept;
    [[nodiscard]] float GetLiveIllumination(const glm::vec2& worldXZ) const noexcept;

    // Coordinate conversions
    [[nodiscard]] static glm::ivec2 WorldToGridCoord(const glm::vec2& worldXZ) noexcept;
    [[nodiscard]] static glm::ivec2 GridToToroidalTexel(const glm::ivec2& gridCoord) noexcept;
    [[nodiscard]] bool IsInActiveWindow(const glm::ivec2& gridCoord) const noexcept;

    // POI Pin Clamping to circular rim
    [[nodiscard]] static glm::vec2 ProjectAndClampPOIPin(const glm::vec2& poiWorldXZ,
                                                         const glm::vec2& viewerWorldXZ,
                                                         float maxWorldRadius,
                                                         bool& outIsClamped) noexcept;

    [[nodiscard]] const std::vector<uint8_t>& GetPixelBuffer() const noexcept { return m_DisplayBuffer; }
    [[nodiscard]] const std::shared_ptr<Texture>& GetFogTexture() const noexcept { return m_FogTexture; }

    [[nodiscard]] const std::vector<MinimapPOIPin>& GetActivePins() const noexcept { return m_ActivePins; }
    void AddPin(const MinimapPOIPin& pin);
    void ClearPins() { m_ActivePins.clear(); }

    void SyncGpuTexture();

private:
    void RebuildDisplayBuffer();
    [[nodiscard]] static uint8_t BiomeToColorID(BiomeType biome) noexcept;

private:
    std::vector<ToroidalCell> m_ToroidalGrid; // 256x256 toroidal cells
    std::vector<uint8_t> m_DisplayBuffer;    // 256x256x4 RGBA8 player-centered display texture
    std::shared_ptr<Texture> m_FogTexture{ nullptr };
    glm::vec2 m_CurrentViewerWorldXZ{ 0.0f };
    glm::ivec2 m_CurrentGridCenter{ 0, 0 };
    std::vector<MinimapPOIPin> m_ActivePins;
    bool m_TextureDirty = true;
};

} // namespace Engine

#endif // UI_MINIMAPFOGSYSTEM_H
