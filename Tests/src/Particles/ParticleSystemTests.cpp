#include <gtest/gtest.h>
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Scene/GameObject.h"

namespace {

TEST(ParticleSystemTest, InitializationAndDefaultState) {
    Engine::GameObject entity("BrazierEmitter");
    auto* ps = entity.AddComponent<Engine::ParticleSystem>(500);

    ASSERT_NE(ps, nullptr);
    EXPECT_EQ(ps->GetParticles().size(), 500u);
    EXPECT_EQ(ps->GetActiveParticleCount(), 0u);
    EXPECT_FALSE(ps->AutoEmit);
    EXPECT_FALSE(ps->EmissiveParticles);
}

TEST(ParticleSystemTest, EmitIncrementsActiveCountAndInitializesParticle) {
    Engine::ParticleSystem ps(100);

    Engine::ParticleProps props;
    props.position = { 1.0f, 2.0f, 3.0f };
    props.velocity = { 0.0f, 1.0f, 0.0f };
    props.lifeTime = 2.0f;
    props.sizeBegin = 0.5f;
    props.sizeEnd = 0.1f;

    ps.Emit(props);
    EXPECT_EQ(ps.GetActiveParticleCount(), 1u);

    const auto& particles = ps.GetParticles();
    EXPECT_TRUE(particles[0].active);
    EXPECT_FLOAT_EQ(particles[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(particles[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(particles[0].position.z, 3.0f);
    EXPECT_FLOAT_EQ(particles[0].lifeTime, 2.0f);
    EXPECT_FLOAT_EQ(particles[0].lifeRemaining, 2.0f);
}

TEST(ParticleSystemTest, UpdateSimulatesMotionAndCompactExpiredParticles) {
    Engine::ParticleSystem ps(10);

    Engine::ParticleProps props;
    props.position = { 0.0f, 0.0f, 0.0f };
    props.velocity = { 0.0f, 5.0f, 0.0f };
    props.velocityVariation = { 0.0f, 0.0f, 0.0f };
    props.lifeTime = 0.2f;

    ps.Emit(props);
    ps.Emit(props);
    EXPECT_EQ(ps.GetActiveParticleCount(), 2u);

    // Update 0.1s -> particles move, remain active
    ps.Update(0.1f);
    EXPECT_EQ(ps.GetActiveParticleCount(), 2u);
    EXPECT_GT(ps.GetParticles()[0].position.y, 0.0f);

    // Update 0.15s (total 0.25s) -> both particles expire and are compacted
    ps.Update(0.15f);
    EXPECT_EQ(ps.GetActiveParticleCount(), 0u);
}

} // namespace

