#ifndef PARTICLES_PARTICLESYSTEM_H
#define PARTICLES_PARTICLESYSTEM_H

#include "Engine/Scene/Component.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Engine {
    // Defines how a particle behaves when spawned
    struct ParticleProps {
        glm::vec3 position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 velocity = { 0.0f, 0.0f, 0.0f };
        glm::vec3 velocityVariation = { 0.0f, 0.0f, 0.0f };

        glm::vec4 colorBegin = { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 colorEnd = { 1.0f, 1.0f, 1.0f, 0.0f };

        float sizeBegin = 0.5f;
        float sizeEnd = 0.0f;
        float sizeVariation = 0.1f;

        float lifeTime = 1.0f;
    };

    class ParticleSystem : public Component {
    public:
        ParticleSystem(uint32_t maxParticles = 1000);

        void Emit(const ParticleProps& particleProps);
        void Update(float deltaTime) override;

        // Optional continuous ambient emission (fire embers, shrine wisps, chimneys)
        bool AutoEmit = false;
        float EmitRate = 8.0f; // Particles per second
        glm::vec3 SpawnOffset = { 0.0f, 0.95f, 0.0f };
        bool EmissiveParticles = false;
        ParticleProps TemplateProps{
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.15f, 0.0f },
            { 0.35f, 0.35f, 0.35f },
            { 1.0f, 0.72f, 0.22f, 0.95f },
            { 0.90f, 0.18f, 0.08f, 0.0f },
            0.12f,
            0.02f,
            0.04f,
            1.15f
        };

        // Expose to the Scene for rendering
        struct Particle {
            glm::vec3 position{ 0.0f };
            glm::vec3 velocity{ 0.0f };
            glm::vec4 colorBegin{ 1.0f }, colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
            float rotation = 0.0f;
            float sizeBegin = 0.5f, sizeEnd = 0.0f;
            float lifeTime = 1.0f;
            float lifeRemaining = 0.0f;
            bool active = false;
        };

        const std::vector<Particle>& GetParticles() const { return m_ParticlePool; }
        uint32_t GetActiveParticleCount() const { return m_ActiveParticleCount; }

    private:
        std::vector<Particle> m_ParticlePool;
        uint32_t m_PoolIndex;
        uint32_t m_ActiveParticleCount = 0;
        float m_EmitAccumulator = 0.0f;
    };
} // namespace Engine

#endif // PARTICLES_PARTICLESYSTEM_H
