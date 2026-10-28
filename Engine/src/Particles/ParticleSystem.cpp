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
    // Helper function to generate a random float between -1.0 and 1.0
    static float RandomFloat() {
        return (float)(rand()) / (float)(RAND_MAX) * 2.0f - 1.0f;
    }

    ParticleSystem::ParticleSystem(uint32_t maxParticles)
        : m_PoolIndex(maxParticles > 0 ? maxParticles - 1 : 0) {
        m_ParticlePool.resize(maxParticles);
        ENGINE_CORE_DEBUG("ParticleSystem created with pool size of {} particles", maxParticles);
    }

    void ParticleSystem::Emit(const ParticleProps& particleProps) {
        if (m_ParticlePool.empty()) {
            ENGINE_CORE_WARN("ParticleSystem::Emit called on an empty particle pool");
            return;
        }

        ENGINE_CORE_TRACE("ParticleSystem: Emitting particle at ({:.2f}, {:.2f}, {:.2f}), pool index: {}",
                          particleProps.position.x, particleProps.position.y, particleProps.position.z, m_PoolIndex);
        Particle& particle = m_ParticlePool[m_PoolIndex];
        if (!particle.active) {
            ++m_ActiveParticleCount;
        }
        particle.active = true;
        particle.position = particleProps.position;
        particle.rotation = RandomFloat() * 2.0f * glm::pi<float>();

        // Add randomness to velocity
        particle.velocity = particleProps.velocity;
        particle.velocity.x += particleProps.velocityVariation.x * RandomFloat();
        particle.velocity.y += particleProps.velocityVariation.y * RandomFloat();
        particle.velocity.z += particleProps.velocityVariation.z * RandomFloat();

        particle.colorBegin = particleProps.colorBegin;
        particle.colorEnd = particleProps.colorEnd;

        particle.sizeBegin = std::max(0.01f, particleProps.sizeBegin + particleProps.sizeVariation * RandomFloat());
        particle.sizeEnd = std::max(0.0f, particleProps.sizeEnd);

        particle.lifeTime = std::max(0.05f, particleProps.lifeTime);
        particle.lifeRemaining = particle.lifeTime;

        // Cycle through the pool index safely
        if (m_PoolIndex == 0) {
            m_PoolIndex = static_cast<uint32_t>(m_ParticlePool.size() - 1);
        } else {
            --m_PoolIndex;
        }
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
                    0.12f * RandomFloat(),
                    0.06f * RandomFloat(),
                    0.12f * RandomFloat()
                );
                Emit(props);
                m_EmitAccumulator -= 1.0f;
            }
        }

        for (auto& particle : m_ParticlePool) {
            if (!particle.active) continue;

            if (particle.lifeRemaining <= 0.0f) {
                particle.active = false;
                if (m_ActiveParticleCount > 0) {
                    --m_ActiveParticleCount;
                }
                continue;
            }

            particle.lifeRemaining -= deltaTime;
            particle.position += particle.velocity * deltaTime;
        }
    }
} // namespace Engine
