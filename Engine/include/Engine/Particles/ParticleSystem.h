#ifndef PARTICLES_PARTICLESYSTEM_H
#define PARTICLES_PARTICLESYSTEM_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/Texture.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Engine {

enum class ParticleOrientation : uint8_t {
    CameraBillboard = 0, // Torch flames, embers, wisps, smoke
    FlatGroundXZ    = 1, // Kinetic impact shockwaves (vfx_impact_ring), weapon cleave arcs
    VelocityAligned = 2  // Directional sparks, blood streaks, flying wood splinters
};

struct FlipbookConfig {
    bool Enabled = false;
    uint32_t Columns = 1;
    uint32_t Rows = 1;
    uint32_t TotalFrames = 1;
    float CyclesOverLifetime = 1.0f; // 1.0 = play once (impact ring); >1.0 = loop (firewall/torch)
    bool RandomStartFrame = false;   // Desynchronizes multiple burning tar walls
};

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

    // Optional explicit rotation controls (appended for aggregate-init backwards compatibility)
    float rotation = 0.0f;
    bool randomRotation = true;
};

class ParticleSystem final : public Component {
public:
    ParticleSystem(uint32_t maxParticles = 1000);

    void Emit(const ParticleProps& particleProps);
    void EmitBurst(const ParticleProps& props, uint32_t count);
    void Update(float deltaTime) override;
    void Reset() noexcept;

    void OnPoolAcquire() override { Reset(); }
    void OnPoolRelease() override { Reset(); }

    // Visuals, orientation & flipbook configuration
    std::shared_ptr<Texture> ParticleTexture = nullptr;
    ParticleOrientation Orientation = ParticleOrientation::CameraBillboard;
    FlipbookConfig Flipbook{};

    // Ballistic & ground collision physics
    float GravityY = 0.0f;                // e.g., -18.0f for vfx_wood_splinters
    float LinearDrag = 0.0f;              // Air resistance slowing burst chips
    bool CollideGroundY = false;          // Bounces particles at GroundPlaneY with GroundBounceRestitution
    float GroundPlaneY = 0.02f;           // Ground collision plane height (default 0.02f)
    float GroundBounceRestitution = 0.4f; // Restitution factor on Y ground bounce [0..1]

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
        uint32_t flipbookStartFrame = 0;
        bool active = false;
    };

    [[nodiscard]] const std::vector<Particle>& GetParticles() const noexcept { return m_ParticlePool; }
    [[nodiscard]] uint32_t GetActiveParticleCount() const noexcept { return m_ActiveParticleCount; }

    // Zero-allocation pure helpers shared between Renderer2D worker threads and unit tests
    [[nodiscard]] static uint32_t ComputeFlipbookFrameIndex(
        const Particle& particle,
        const FlipbookConfig& flipbook) noexcept;

    static void ComputeFlipbookUVs(
        uint32_t frameIndex,
        const FlipbookConfig& flipbook,
        std::span<glm::vec2, 4> outUVs) noexcept;

private:
    std::vector<Particle> m_ParticlePool;
    uint32_t m_PoolIndex{0};
    uint32_t m_ActiveParticleCount{0};
    float m_EmitAccumulator{0.0f};
    uint32_t m_RngState{0x853c49e6};
};
} // namespace Engine

#endif // PARTICLES_PARTICLESYSTEM_H
