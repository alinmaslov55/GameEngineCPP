#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/constants.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/compatibility.hpp>

namespace Engine {
// Fast 32-bit Xorshift PRNG (lock-free, zero syscalls)
static inline uint32_t FastRandomUint(uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Returns float in [-1.0, 1.0]
static inline float FastRandomFloat(uint32_t& state) noexcept {
    return static_cast<float>(FastRandomUint(state)) * (1.0f / 4294967296.0f) * 2.0f - 1.0f;
}

ParticleSystem::ParticleSystem(uint32_t maxParticles)
    : m_PoolIndex(0) {
    m_ParticlePool.resize(maxParticles);
    ENGINE_CORE_DEBUG("ParticleSystem created with pool size of {} particles", maxParticles);
}

void ParticleSystem::Emit(const ParticleProps& particleProps) {
    if (m_ParticlePool.empty()) {
        ENGINE_CORE_WARN("ParticleSystem::Emit called on an empty particle pool");
        return;
    }

    uint32_t targetIndex = 0;
    if (m_ActiveParticleCount < m_ParticlePool.size()) {
        targetIndex = m_ActiveParticleCount++;
    } else {
        // Pool saturated, advance ring index across full capacity
        targetIndex = m_PoolIndex;
        m_PoolIndex = (m_PoolIndex + 1) % static_cast<uint32_t>(m_ParticlePool.size());
    }

    Particle& particle = m_ParticlePool[targetIndex];
    particle.active = true;
    particle.position = particleProps.position;
    particle.rotation = particleProps.randomRotation
        ? (FastRandomFloat(m_RngState) * 2.0f * glm::pi<float>())
        : particleProps.rotation;

    // Add randomness to velocity
    particle.velocity = particleProps.velocity;
    particle.velocity.x += particleProps.velocityVariation.x * FastRandomFloat(m_RngState);
    particle.velocity.y += particleProps.velocityVariation.y * FastRandomFloat(m_RngState);
    particle.velocity.z += particleProps.velocityVariation.z * FastRandomFloat(m_RngState);

    particle.colorBegin = particleProps.colorBegin;
    particle.colorEnd = particleProps.colorEnd;

    particle.sizeBegin = std::max(0.01f, particleProps.sizeBegin + particleProps.sizeVariation * FastRandomFloat(m_RngState));
    particle.sizeEnd = std::max(0.0f, particleProps.sizeEnd);

    particle.lifeTime = std::max(0.05f, particleProps.lifeTime);
    particle.lifeRemaining = particle.lifeTime;

    if (Flipbook.Enabled && Flipbook.RandomStartFrame) {
        const uint32_t maxGridFrames = std::max(1u, Flipbook.Columns) * std::max(1u, Flipbook.Rows);
        const uint32_t totalFrames = std::clamp(Flipbook.TotalFrames, 1u, maxGridFrames);
        particle.flipbookStartFrame = FastRandomUint(m_RngState) % totalFrames;
    } else {
        particle.flipbookStartFrame = 0u;
    }
}

void ParticleSystem::EmitBurst(const ParticleProps& props, uint32_t count) {
    if (count == 0) {
        return;
    }
    if (m_ParticlePool.empty()) {
        ENGINE_CORE_WARN("ParticleSystem::EmitBurst called on an empty particle pool");
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        Emit(props);
    }
}

void ParticleSystem::Update(float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    if (AutoEmit && EmitRate > 0.0f) {
        m_EmitAccumulator = std::min(m_EmitAccumulator + deltaTime * EmitRate, 32.0f);
        glm::vec3 basePos{ 0.0f };
        if (auto* owner = GetGameObject()) {
            if (auto* transform = owner->GetTransform()) {
                basePos = transform->GetWorldPosition();
            }
        }
        while (m_EmitAccumulator >= 1.0f) {
            ParticleProps props = TemplateProps;
            props.position = basePos + SpawnOffset + glm::vec3(
                0.12f * FastRandomFloat(m_RngState),
                0.06f * FastRandomFloat(m_RngState),
                0.12f * FastRandomFloat(m_RngState)
            );
            Emit(props);
            m_EmitAccumulator -= 1.0f;
        }
    }

    const float dragDamping = (LinearDrag > 0.0f) ? (1.0f / (1.0f + LinearDrag * deltaTime)) : 1.0f;
    const float clampedRestitution = std::clamp(GroundBounceRestitution, 0.0f, 1.0f);

    // Active particles are contiguously stored in [0 .. m_ActiveParticleCount - 1]
    for (uint32_t i = 0; i < m_ActiveParticleCount; ) {
        Particle& particle = m_ParticlePool[i];
        particle.lifeRemaining -= deltaTime;

        if (particle.lifeRemaining <= 0.0f) {
            particle.active = false;
            if (m_ActiveParticleCount > 1 && i != m_ActiveParticleCount - 1) {
                m_ParticlePool[i] = m_ParticlePool[m_ActiveParticleCount - 1];
            }
            --m_ActiveParticleCount;
            // Re-evaluate the swapped element at index i
        } else {
            if (GravityY != 0.0f) {
                particle.velocity.y += GravityY * deltaTime;
            }
            if (LinearDrag > 0.0f) {
                particle.velocity *= dragDamping;
            }
            particle.position += particle.velocity * deltaTime;

            if (CollideGroundY && particle.position.y < GroundPlaneY) {
                particle.position.y = GroundPlaneY;
                if (particle.velocity.y < 0.0f) {
                    particle.velocity.y = -particle.velocity.y * clampedRestitution;
                    if (std::abs(particle.velocity.y) < 0.15f) {
                        particle.velocity.y = 0.0f;
                    }
                    particle.velocity.x *= 0.75f;
                    particle.velocity.z *= 0.75f;
                }
            }
            ++i;
        }
    }
}

void ParticleSystem::Reset() noexcept {
    for (uint32_t i = 0; i < m_ActiveParticleCount; ++i) {
        m_ParticlePool[i].active = false;
        m_ParticlePool[i].lifeRemaining = 0.0f;
        m_ParticlePool[i].flipbookStartFrame = 0u;
    }
    m_ActiveParticleCount = 0;
    m_PoolIndex = 0;
    m_EmitAccumulator = 0.0f;
}

uint32_t ParticleSystem::ComputeFlipbookFrameIndex(
    const Particle& particle,
    const FlipbookConfig& flipbook) noexcept {
    if (!flipbook.Enabled) {
        return 0u;
    }

    const uint32_t cols = std::max(1u, flipbook.Columns);
    const uint32_t rows = std::max(1u, flipbook.Rows);
    const uint32_t maxGridFrames = cols * rows;
    const uint32_t totalFrames = std::clamp(flipbook.TotalFrames, 1u, maxGridFrames);

    if (totalFrames <= 1u) {
        return 0u;
    }

    const float lifeTime = std::max(particle.lifeTime, 1e-5f);
    const float ageNorm = std::clamp(1.0f - (particle.lifeRemaining / lifeTime), 0.0f, 1.0f);
    const float cycles = std::max(0.0f, flipbook.CyclesOverLifetime);
    const float rawFrame = ageNorm * static_cast<float>(totalFrames) * cycles;
    const uint32_t frameOffset = static_cast<uint32_t>(std::floor(rawFrame));

    if (cycles <= 1.0f && !flipbook.RandomStartFrame) {
        return std::min(frameOffset, totalFrames - 1u);
    }

    return (particle.flipbookStartFrame + frameOffset) % totalFrames;
}

void ParticleSystem::ComputeFlipbookUVs(
    uint32_t frameIndex,
    const FlipbookConfig& flipbook,
    std::span<glm::vec2, 4> outUVs) noexcept {
    if (!flipbook.Enabled) {
        outUVs[0] = { 0.0f, 0.0f };
        outUVs[1] = { 1.0f, 0.0f };
        outUVs[2] = { 1.0f, 1.0f };
        outUVs[3] = { 0.0f, 1.0f };
        return;
    }

    const uint32_t cols = std::max(1u, flipbook.Columns);
    const uint32_t rows = std::max(1u, flipbook.Rows);
    const uint32_t maxGridFrames = cols * rows;
    const uint32_t totalFrames = std::clamp(flipbook.TotalFrames, 1u, maxGridFrames);
    const uint32_t clampedFrame = std::min(frameIndex, totalFrames - 1u);

    const uint32_t col = clampedFrame % cols;
    const uint32_t rowFromTop = clampedFrame / cols;

    const float invCols = 1.0f / static_cast<float>(cols);
    const float invRows = 1.0f / static_cast<float>(rows);

    const float uMin = static_cast<float>(col) * invCols;
    const float uMax = static_cast<float>(col + 1u) * invCols;
    // OpenGL images are vertically flipped on load (row 0 = top of texture -> v = 1.0)
    const float vMax = 1.0f - static_cast<float>(rowFromTop) * invRows;
    const float vMin = 1.0f - static_cast<float>(rowFromTop + 1u) * invRows;

    outUVs[0] = { uMin, vMin };
    outUVs[1] = { uMax, vMin };
    outUVs[2] = { uMax, vMax };
    outUVs[3] = { uMin, vMax };
}

} // namespace Engine
