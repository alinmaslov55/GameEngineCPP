#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Utils/Logger.h"
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
        if (m_ParticlePool.empty()) return;

        Particle& particle = m_ParticlePool[m_PoolIndex];
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

        particle.sizeBegin = particleProps.sizeBegin + particleProps.sizeVariation * RandomFloat();
        particle.sizeEnd = particleProps.sizeEnd;

        particle.lifeTime = particleProps.lifeTime;
        particle.lifeRemaining = particleProps.lifeTime;

        // Cycle through the pool index safely
        if (m_PoolIndex == 0) {
            m_PoolIndex = static_cast<uint32_t>(m_ParticlePool.size() - 1);
        } else {
            --m_PoolIndex;
        }
    }

    void ParticleSystem::Update(float deltaTime) {
        for (auto& particle : m_ParticlePool) {
            if (!particle.active) continue;

            if (particle.lifeRemaining <= 0.0f) {
                particle.active = false;
                continue;
            }

            particle.lifeRemaining -= deltaTime;
            particle.position += particle.velocity * deltaTime;
        }
    }
} // namespace Engine

