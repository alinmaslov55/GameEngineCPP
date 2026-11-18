#ifndef PARTICLES_DECALMANAGER_H
#define PARTICLES_DECALMANAGER_H

#include "Engine/Math/Frustum.h"
#include "Engine/Renderer/SubTexture2D.h"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <vector>

namespace Engine {

struct GroundDecal {
    glm::vec3 Position{ 0.0f }; // Y auto-lifted +0.005f + subtle index micro-offset to prevent Z-fighting
    glm::vec2 SizeXZ{ 1.2f };
    float RotationYRadians = 0.0f;
    glm::vec4 Color{ 1.0f };
    std::shared_ptr<SubTexture2D> SubTex;
    float Age = 0.0f;
    float MaxLifetime = 45.0f;
    float FadeOutDuration = 4.0f;
};

class DecalManager {
public:
    static constexpr float kBaseLiftY = 0.005f;
    static constexpr float kMicroOffsetStepY = 0.00001f;

    explicit DecalManager(uint32_t maxDecals = 512);

    void SpawnDecal(const GroundDecal& decal); // Overwrites oldest slot in O(1) when full
    void Update(float deltaTime);
    void Render(const Frustum& camFrustum);    // Called inside SceneRenderer::RenderPassGroundPlane
    void ClearInChunk(int32_t cx, int32_t cz, float chunkSize);
    void Clear() noexcept;

    [[nodiscard]] uint32_t GetActiveCount() const noexcept { return m_Count; }
    [[nodiscard]] uint32_t GetCapacity() const noexcept { return m_Capacity; }
    [[nodiscard]] const GroundDecal& GetActiveDecal(uint32_t logicalIndex) const noexcept;
    [[nodiscard]] uint32_t GetLastRenderedCount() const noexcept { return m_LastRenderedCount; }
    [[nodiscard]] uint32_t GetLastCulledCount() const noexcept { return m_LastCulledCount; }

    [[nodiscard]] static float ComputeEffectiveAlpha(const GroundDecal& decal) noexcept;

private:
    std::vector<GroundDecal> m_Decals;
    uint32_t m_Capacity = 0;
    uint32_t m_Head = 0;
    uint32_t m_Count = 0;
    uint32_t m_SpawnSequence = 0;
    uint32_t m_LastRenderedCount = 0;
    uint32_t m_LastCulledCount = 0;
};

} // namespace Engine

#endif // PARTICLES_DECALMANAGER_H
