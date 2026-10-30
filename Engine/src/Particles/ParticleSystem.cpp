#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"
#include <algorithm>
#include <cstdlib>
#include <glm/gtc/constants.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/compatibility.hpp>

namespace Engine {
// Fast 32-bit Xorshift PRNG returning float between -1.0 and 1.0 (lock-free, zero syscalls)
static inline float FastRandomFloat(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) * (1.0f / 4294967296.0f) * 2.0f - 1.0f;
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
        // Pool saturated, recycle oldest particle at index 0
        targetIndex = 0;
    }

    ENGINE_CORE_TRACE("ParticleSystem: Emitting particle at ({:.2f}, {:.2f}, {:.2f}), pool index: {}",
                        particleProps.position.x, particleProps.position.y, particleProps.position.z, targetIndex);
    Particle& particle = m_ParticlePool[targetIndex];
    particle.active = true;
    particle.position = particleProps.position;
    particle.rotation = FastRandomFloat(m_RngState) * 2.0f * glm::pi<float>();

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
}

void ParticleSystem::Update(float deltaTime) {
    ENGINE_CORE_TRACE("ParticleSystem::Update (deltaTime: {:.4f}s)", deltaTime);

    if (AutoEmit && EmitRate > 0.0f && deltaTime > 0.0f) {
        m_EmitAccumulator = std::min(m_EmitAccumulator + deltaTime * EmitRate, 32.0f);
        glm::vec3 basePos{ 0.0f };
        if (auto* owner = GetGameObject()) {
            if (auto* transform = owner->GetTransform()) {
                basePos = transform->position;
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
            particle.position += particle.velocity * deltaTime;
            ++i;
        }
    }
}
} // namespace Engine
