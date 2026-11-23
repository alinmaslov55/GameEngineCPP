#include "Engine/Particles/DecalManager.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace Engine {

DecalManager::DecalManager(uint32_t maxDecals)
    : m_Capacity(maxDecals) {
    m_Decals.resize(m_Capacity);
    ENGINE_CORE_DEBUG("DecalManager created with ring capacity of {} ground decals", m_Capacity);
}

void DecalManager::SpawnDecal(const GroundDecal& decal) {
    if (m_Capacity == 0u) {
        return;
    }

    GroundDecal entry = decal;
    entry.Position.y += kBaseLiftY + static_cast<float>(m_SpawnSequence & 0xFFu) * kMicroOffsetStepY;
    ++m_SpawnSequence;

    uint32_t writeSlot = 0u;
    if (m_Count < m_Capacity) {
        writeSlot = (m_Head + m_Count) % m_Capacity;
        ++m_Count;
    } else {
        // Pool full: overwrite the oldest decal in strict O(1)
        writeSlot = m_Head;
        m_Head = (m_Head + 1u) % m_Capacity;
    }

    m_Decals[writeSlot] = std::move(entry);
}

void DecalManager::Update(float deltaTime) {
    if (deltaTime <= 0.0f || m_Count == 0u || m_Capacity == 0u) {
        return;
    }

    // Step 1: Drain expired decals from m_Head in strict O(1) amortized time
    while (m_Count > 0u) {
        GroundDecal& headDecal = m_Decals[m_Head];
        headDecal.Age += deltaTime;
        if (headDecal.MaxLifetime > 0.0f && headDecal.Age >= headDecal.MaxLifetime) {
            headDecal = GroundDecal{};
            m_Head = (m_Head + 1u) % m_Capacity;
            --m_Count;
        } else {
            // Head decal remains alive; age was advanced
            break;
        }
    }

    if (m_Count == 0u) {
        m_Head = 0u;
        return;
    }

    // Step 2: For remaining decals (from logical slot 1 onwards), advance age and compact if out-of-order expired
    bool hasOutOfOrderExpiration = false;
    for (uint32_t k = 1u; k < m_Count; ++k) {
        const uint32_t idx = (m_Head + k) % m_Capacity;
        m_Decals[idx].Age += deltaTime;
        if (m_Decals[idx].MaxLifetime > 0.0f && m_Decals[idx].Age >= m_Decals[idx].MaxLifetime) {
            hasOutOfOrderExpiration = true;
        }
    }

    if (!hasOutOfOrderExpiration) {
        return;
    }

    // Out-of-order expiration occurred: compact active elements contiguously starting from m_Head
    uint32_t writeK = 0u;
    for (uint32_t k = 0u; k < m_Count; ++k) {
        const uint32_t srcIdx = (m_Head + k) % m_Capacity;
        GroundDecal& decal = m_Decals[srcIdx];
        const bool expired = (decal.MaxLifetime > 0.0f && decal.Age >= decal.MaxLifetime);
        if (!expired) {
            if (writeK != k) {
                const uint32_t dstIdx = (m_Head + writeK) % m_Capacity;
                m_Decals[dstIdx] = std::move(decal);
            }
            ++writeK;
        }
    }

    // Release SubTexture2D shared_ptr references on compacted tail slots
    for (uint32_t k = writeK; k < m_Count; ++k) {
        const uint32_t deadIdx = (m_Head + k) % m_Capacity;
        m_Decals[deadIdx] = GroundDecal{};
    }

    m_Count = writeK;
    if (m_Count == 0u) {
        m_Head = 0u;
    }
}

void DecalManager::Render(const Frustum& camFrustum) {
    m_LastRenderedCount = 0u;
    m_LastCulledCount = 0u;

    if (m_Count == 0u || m_Capacity == 0u) {
        return;
    }

    // Draw oldest-to-newest so fresh splatters layer cleanly over older ones
    for (uint32_t k = 0u; k < m_Count; ++k) {
        const uint32_t idx = (m_Head + k) % m_Capacity;
        const GroundDecal& decal = m_Decals[idx];

        const float alpha = ComputeEffectiveAlpha(decal);
        if (alpha <= 1e-4f) {
            ++m_LastCulledCount;
            continue;
        }

        const float radius = 0.5f * std::sqrt(decal.SizeXZ.x * decal.SizeXZ.x +
                                              decal.SizeXZ.y * decal.SizeXZ.y);
        if (!camFrustum.IntersectsSphere(decal.Position, radius)) {
            ++m_LastCulledCount;
            continue;
        }

        glm::vec4 fadedColor = decal.Color;
        fadedColor.a = alpha;

        Renderer2D::DrawGroundTile(
            decal.Position,
            decal.SizeXZ,
            decal.SubTex,
            fadedColor,
            /*alphaCutoff=*/0.0f,
            /*emissive=*/0.0f,
            /*shadowReceiverOffset=*/0.02f,
            decal.RotationYRadians
        );
        ++m_LastRenderedCount;
    }
}

void DecalManager::ClearInChunk(int32_t cx, int32_t cz, float chunkSize) {
    if (m_Count == 0u || m_Capacity == 0u || chunkSize <= 0.0f) {
        return;
    }

    const float minX = static_cast<float>(cx) * chunkSize;
    const float maxX = static_cast<float>(cx + 1) * chunkSize;
    const float minZ = static_cast<float>(cz) * chunkSize;
    const float maxZ = static_cast<float>(cz + 1) * chunkSize;

    uint32_t writeK = 0u;
    for (uint32_t k = 0u; k < m_Count; ++k) {
        const uint32_t srcIdx = (m_Head + k) % m_Capacity;
        GroundDecal& decal = m_Decals[srcIdx];

        const bool inChunk = (decal.Position.x >= minX && decal.Position.x < maxX &&
                              decal.Position.z >= minZ && decal.Position.z < maxZ);
        if (!inChunk) {
            if (writeK != k) {
                const uint32_t dstIdx = (m_Head + writeK) % m_Capacity;
                m_Decals[dstIdx] = std::move(decal);
            }
            ++writeK;
        }
    }

    for (uint32_t k = writeK; k < m_Count; ++k) {
        const uint32_t deadIdx = (m_Head + k) % m_Capacity;
        m_Decals[deadIdx] = GroundDecal{};
    }

    m_Count = writeK;
    if (m_Count == 0u) {
        m_Head = 0u;
    }
}

void DecalManager::Clear() noexcept {
    for (uint32_t k = 0u; k < m_Count; ++k) {
        const uint32_t idx = (m_Head + k) % m_Capacity;
        m_Decals[idx] = GroundDecal{};
    }
    m_Head = 0u;
    m_Count = 0u;
    m_LastRenderedCount = 0u;
    m_LastCulledCount = 0u;
}

const GroundDecal& DecalManager::GetActiveDecal(uint32_t logicalIndex) const noexcept {
    assert(logicalIndex < m_Count && "DecalManager::GetActiveDecal index out of bounds");
    return m_Decals[(m_Head + logicalIndex) % m_Capacity];
}

float DecalManager::ComputeEffectiveAlpha(const GroundDecal& decal) noexcept {
    if (decal.MaxLifetime <= 0.0f) {
        return std::clamp(decal.Color.a, 0.0f, 1.0f);
    }

    const float remaining = decal.MaxLifetime - decal.Age;
    if (remaining <= 0.0f) {
        return 0.0f;
    }

    if (decal.FadeOutDuration <= 0.0f || remaining >= decal.FadeOutDuration) {
        return std::clamp(decal.Color.a, 0.0f, 1.0f);
    }

    const float fadeFactor = std::clamp(remaining / decal.FadeOutDuration, 0.0f, 1.0f);
    return std::clamp(decal.Color.a * fadeFactor, 0.0f, 1.0f);
}

} // namespace Engine
