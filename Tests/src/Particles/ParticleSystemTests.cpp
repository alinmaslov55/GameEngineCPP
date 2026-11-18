#include <gtest/gtest.h>
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"

#include <cmath>
#include <unordered_set>
#include <vector>

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

TEST(ParticleSystemTest, PoolSaturationWrapsRingIndexAcrossFullCapacity) {
    Engine::ParticleSystem ps(3);

    Engine::ParticleProps props;
    props.velocity = { 0.0f, 0.0f, 0.0f };
    props.velocityVariation = { 0.0f, 0.0f, 0.0f };
    props.lifeTime = 10.0f;

    // Fill pool to capacity (3 particles)
    props.position = { 1.0f, 0.0f, 0.0f };
    ps.Emit(props);
    props.position = { 2.0f, 0.0f, 0.0f };
    ps.Emit(props);
    props.position = { 3.0f, 0.0f, 0.0f };
    ps.Emit(props);
    EXPECT_EQ(ps.GetActiveParticleCount(), 3u);

    // 4th particle emitted while saturated: must overwrite ring index 0
    props.position = { 10.0f, 0.0f, 0.0f };
    ps.Emit(props);
    EXPECT_FLOAT_EQ(ps.GetParticles()[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[1].position.x, 2.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[2].position.x, 3.0f);

    // 5th particle emitted while saturated: must advance ring to index 1 (not overwrite index 0 again)
    props.position = { 20.0f, 0.0f, 0.0f };
    ps.Emit(props);
    EXPECT_FLOAT_EQ(ps.GetParticles()[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[1].position.x, 20.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[2].position.x, 3.0f);

    // 6th particle emitted while saturated: must advance ring to index 2
    props.position = { 30.0f, 0.0f, 0.0f };
    ps.Emit(props);
    EXPECT_FLOAT_EQ(ps.GetParticles()[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[1].position.x, 20.0f);
    EXPECT_FLOAT_EQ(ps.GetParticles()[2].position.x, 30.0f);
}

TEST(ParticleSystemTest, EmitBurstWoodSplintersGravityGroundCollisionAndFlipbookMonotonicity) {
    Engine::ParticleSystem ps(64);
    ps.Orientation = Engine::ParticleOrientation::VelocityAligned;
    ps.GravityY = -18.0f;
    ps.LinearDrag = 1.25f;
    ps.CollideGroundY = true;
    ps.GroundPlaneY = 0.02f;
    ps.GroundBounceRestitution = 0.4f;
    ps.Flipbook.Enabled = true;
    ps.Flipbook.Columns = 4;
    ps.Flipbook.Rows = 4;
    ps.Flipbook.TotalFrames = 16;
    ps.Flipbook.CyclesOverLifetime = 1.0f;
    ps.Flipbook.RandomStartFrame = false;

    Engine::ParticleProps splinters;
    splinters.position = { 0.0f, 0.8f, 0.0f };
    splinters.velocity = { 0.0f, 3.5f, 0.0f };
    splinters.velocityVariation = { 4.0f, 2.5f, 4.0f };
    splinters.sizeBegin = 0.25f;
    splinters.sizeEnd = 0.12f;
    splinters.sizeVariation = 0.05f;
    splinters.lifeTime = 1.2f; // Survives full 1.0s simulation window

    ps.EmitBurst(splinters, 32u);
    ASSERT_EQ(ps.GetActiveParticleCount(), 32u);

    std::vector<uint32_t> prevFrames(32, 0u);
    bool observedGroundBounce = false;
    uint32_t maxObservedFrame = 0u;

    // Step 1.0s at 60 Hz
    constexpr int kSteps = 60;
    constexpr float kDt = 1.0f / 60.0f;
    for (int step = 0; step < kSteps; ++step) {
        ps.Update(kDt);
        ASSERT_EQ(ps.GetActiveParticleCount(), 32u);

        const auto& pool = ps.GetParticles();
        for (uint32_t i = 0; i < 32u; ++i) {
            const auto& p = pool[i];
            EXPECT_GE(p.position.y, 0.0f) << "Particle " << i << " fell below Y=0 at step " << step;
            EXPECT_GE(p.position.y, ps.GroundPlaneY - 1e-5f);

            if (std::abs(p.position.y - ps.GroundPlaneY) < 1e-4f && p.velocity.y >= 0.0f) {
                observedGroundBounce = true;
            }

            const uint32_t frameIdx = Engine::ParticleSystem::ComputeFlipbookFrameIndex(p, ps.Flipbook);
            EXPECT_LT(frameIdx, ps.Flipbook.TotalFrames);
            EXPECT_GE(frameIdx, prevFrames[i])
                << "Flipbook frame regressed for particle " << i << " at step " << step;
            prevFrames[i] = frameIdx;
            maxObservedFrame = std::max(maxObservedFrame, frameIdx);
        }
    }

    EXPECT_TRUE(observedGroundBounce);
    // At t = 1.0s of 1.2s lifetime: ageNorm = 1.0/1.2 = 0.8333 -> floor(0.8333 * 16) = 13
    EXPECT_EQ(maxObservedFrame, 13u);

    // Step close to expiration (t = 1.18s) -> must reach final frame 15 without wrapping back to 0
    ps.Update(0.18f);
    ASSERT_EQ(ps.GetActiveParticleCount(), 32u);
    for (uint32_t i = 0; i < 32u; ++i) {
        const uint32_t frameIdx = Engine::ParticleSystem::ComputeFlipbookFrameIndex(ps.GetParticles()[i], ps.Flipbook);
        EXPECT_EQ(frameIdx, 15u);
    }
}

TEST(ParticleSystemTest, FlipbookUVSlicingAndLoopingWithRandomStartFrame) {
    Engine::FlipbookConfig fb;
    fb.Enabled = true;
    fb.Columns = 4;
    fb.Rows = 4;
    fb.TotalFrames = 16;
    fb.CyclesOverLifetime = 1.0f;
    fb.RandomStartFrame = false;

    glm::vec2 uvs[4];

    // Frame 0: top-left of image (OpenGL flipped V => v in [0.75, 1.0])
    Engine::ParticleSystem::ComputeFlipbookUVs(0u, fb, uvs);
    EXPECT_NEAR(uvs[0].x, 0.0f,  1e-5f);
    EXPECT_NEAR(uvs[0].y, 0.75f, 1e-5f);
    EXPECT_NEAR(uvs[2].x, 0.25f, 1e-5f);
    EXPECT_NEAR(uvs[2].y, 1.0f,  1e-5f);

    // Frame 3: top-right of image (col 3, row 0)
    Engine::ParticleSystem::ComputeFlipbookUVs(3u, fb, uvs);
    EXPECT_NEAR(uvs[0].x, 0.75f, 1e-5f);
    EXPECT_NEAR(uvs[0].y, 0.75f, 1e-5f);
    EXPECT_NEAR(uvs[2].x, 1.0f,  1e-5f);
    EXPECT_NEAR(uvs[2].y, 1.0f,  1e-5f);

    // Frame 4: second row from top, leftmost (col 0, row 1)
    Engine::ParticleSystem::ComputeFlipbookUVs(4u, fb, uvs);
    EXPECT_NEAR(uvs[0].x, 0.0f,  1e-5f);
    EXPECT_NEAR(uvs[0].y, 0.5f,  1e-5f);
    EXPECT_NEAR(uvs[2].x, 0.25f, 1e-5f);
    EXPECT_NEAR(uvs[2].y, 0.75f, 1e-5f);

    // Frame 15: bottom-right of image (col 3, row 3 => v in [0.0, 0.25])
    Engine::ParticleSystem::ComputeFlipbookUVs(15u, fb, uvs);
    EXPECT_NEAR(uvs[0].x, 0.75f, 1e-5f);
    EXPECT_NEAR(uvs[0].y, 0.0f,  1e-5f);
    EXPECT_NEAR(uvs[2].x, 1.0f,  1e-5f);
    EXPECT_NEAR(uvs[2].y, 0.25f, 1e-5f);

    // Looping playback (CyclesOverLifetime = 2.0) wraps cleanly modulo TotalFrames
    fb.CyclesOverLifetime = 2.0f;
    Engine::ParticleSystem::Particle p;
    p.lifeTime = 1.0f;
    p.lifeRemaining = 0.25f; // ageNorm = 0.75 -> 0.75 * 16 * 2 = 24 -> 24 % 16 = 8
    EXPECT_EQ(Engine::ParticleSystem::ComputeFlipbookFrameIndex(p, fb), 8u);

    // RandomStartFrame desynchronizes a burst of firewall particles
    Engine::ParticleSystem firewallPs(64);
    firewallPs.Flipbook = fb;
    firewallPs.Flipbook.RandomStartFrame = true;

    Engine::ParticleProps fireProps;
    fireProps.lifeTime = 2.0f;
    firewallPs.EmitBurst(fireProps, 32u);

    std::unordered_set<uint32_t> distinctStartFrames;
    for (uint32_t i = 0; i < firewallPs.GetActiveParticleCount(); ++i) {
        const uint32_t startFrame = firewallPs.GetParticles()[i].flipbookStartFrame;
        EXPECT_LT(startFrame, 16u);
        distinctStartFrames.insert(startFrame);
    }
    EXPECT_GT(distinctStartFrames.size(), 4u);
}

TEST(ParticleSystemTest, ParticleOrientationsBillboardFlatGroundXZAndVelocityAligned) {
    const glm::vec3 camRight = { 1.0f, 0.0f, 0.0f };
    const glm::vec3 camUp = glm::normalize(glm::vec3(0.0f, 0.766f, -0.6428f)); // ~40 deg pitch camera
    const glm::vec3 pos = { 3.0f, 0.5f, -2.0f };
    const float size = 2.0f;

    glm::vec3 corners[4];

    // 1. CameraBillboard: centered at pos, aligned with camRight & camUp
    Engine::ComputeParticleCorners(pos, { 0.0f, 0.0f, 0.0f }, size, 0.0f,
                                   Engine::ParticleOrientation::CameraBillboard, camRight, camUp, corners);
    const glm::vec3 bbCenter = 0.25f * (corners[0] + corners[1] + corners[2] + corners[3]);
    EXPECT_NEAR(bbCenter.x, pos.x, 1e-5f);
    EXPECT_NEAR(bbCenter.y, pos.y, 1e-5f);
    EXPECT_NEAR(bbCenter.z, pos.z, 1e-5f);
    EXPECT_NEAR(glm::length(corners[1] - corners[0]), size, 1e-5f);
    EXPECT_NEAR(glm::length(corners[3] - corners[0]), size, 1e-5f);

    // 2. FlatGroundXZ: all 4 corners lie strictly at Y = pos.y, facing +Y
    Engine::ComputeParticleCorners(pos, { 5.0f, 2.0f, 0.0f }, size, 0.0f,
                                   Engine::ParticleOrientation::FlatGroundXZ, camRight, camUp, corners);
    for (int c = 0; c < 4; ++c) {
        EXPECT_NEAR(corners[c].y, pos.y, 1e-5f);
    }
    const glm::vec3 groundNormal = glm::normalize(glm::cross(corners[1] - corners[0], corners[3] - corners[0]));
    EXPECT_NEAR(groundNormal.x, 0.0f, 1e-5f);
    EXPECT_NEAR(groundNormal.y, 1.0f, 1e-5f);
    EXPECT_NEAR(groundNormal.z, 0.0f, 1e-5f);

    // 3. VelocityAligned: +V edge aligns strictly with normalize(velocity) and faces camera
    const glm::vec3 vel = { 6.0f, 3.0f, -2.0f };
    const glm::vec3 expectedDir = glm::normalize(vel);
    const glm::vec3 viewNormal = glm::normalize(glm::cross(camRight, camUp));
    Engine::ComputeParticleCorners(pos, vel, size, 0.0f,
                                   Engine::ParticleOrientation::VelocityAligned, camRight, camUp, corners);
    const glm::vec3 leadingMid = 0.5f * (corners[2] + corners[3]);
    const glm::vec3 trailingMid = 0.5f * (corners[0] + corners[1]);
    const glm::vec3 actualDir = glm::normalize(leadingMid - trailingMid);
    EXPECT_NEAR(actualDir.x, expectedDir.x, 1e-5f);
    EXPECT_NEAR(actualDir.y, expectedDir.y, 1e-5f);
    EXPECT_NEAR(actualDir.z, expectedDir.z, 1e-5f);
    const glm::vec3 quadNormal = glm::normalize(glm::cross(corners[1] - corners[0], corners[3] - corners[0]));
    EXPECT_GT(glm::dot(quadNormal, viewNormal), 0.0f);

    // 4. VelocityAligned degenerate fallback (zero velocity & velocity parallel to viewNormal) must not NaN
    Engine::ComputeParticleCorners(pos, { 0.0f, 0.0f, 0.0f }, size, 0.5f,
                                   Engine::ParticleOrientation::VelocityAligned, camRight, camUp, corners);
    for (int c = 0; c < 4; ++c) {
        EXPECT_TRUE(std::isfinite(corners[c].x));
        EXPECT_TRUE(std::isfinite(corners[c].y));
        EXPECT_TRUE(std::isfinite(corners[c].z));
    }

    Engine::ComputeParticleCorners(pos, viewNormal * 5.0f, size, 0.5f,
                                   Engine::ParticleOrientation::VelocityAligned, camRight, camUp, corners);
    for (int c = 0; c < 4; ++c) {
        EXPECT_TRUE(std::isfinite(corners[c].x));
        EXPECT_TRUE(std::isfinite(corners[c].y));
        EXPECT_TRUE(std::isfinite(corners[c].z));
    }
}

TEST(ParticleSystemTest, LinearDragDeceleratesParticlesWithoutReversal) {
    Engine::ParticleSystem ps(8);
    ps.LinearDrag = 4.0f;

    Engine::ParticleProps props;
    props.position = { 0.0f, 1.0f, 0.0f };
    props.velocity = { 10.0f, 0.0f, -6.0f };
    props.velocityVariation = { 0.0f, 0.0f, 0.0f };
    props.lifeTime = 5.0f;

    ps.Emit(props);
    ASSERT_EQ(ps.GetActiveParticleCount(), 1u);

    // Large single-step update must slow down without reversing direction
    ps.Update(1.0f);
    ASSERT_EQ(ps.GetActiveParticleCount(), 1u);

    const auto& p = ps.GetParticles()[0];
    EXPECT_GT(p.velocity.x, 0.0f);
    EXPECT_LT(p.velocity.x, 10.0f);
    EXPECT_LT(p.velocity.z, 0.0f);
    EXPECT_GT(p.velocity.z, -6.0f);
    // Exact check for 1 / (1 + 4 * 1) = 0.2
    EXPECT_NEAR(p.velocity.x, 2.0f, 1e-5f);
    EXPECT_NEAR(p.velocity.z, -1.2f, 1e-5f);
}

TEST(ParticleSystemTest, SceneSerializerParticleSystemVFX01RoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("VFX01SerializerTest");
    auto* go = srcScene->CreateGameObject("SplinterEmitter");
    auto* ps = go->AddComponent<Engine::ParticleSystem>(256);
    ps->Orientation = Engine::ParticleOrientation::FlatGroundXZ;
    ps->GravityY = -18.0f;
    ps->LinearDrag = 2.5f;
    ps->CollideGroundY = true;
    ps->GroundPlaneY = 0.03f;
    ps->GroundBounceRestitution = 0.45f;
    ps->Flipbook.Enabled = true;
    ps->Flipbook.Columns = 4;
    ps->Flipbook.Rows = 3;
    ps->Flipbook.TotalFrames = 12;
    ps->Flipbook.CyclesOverLifetime = 2.5f;
    ps->Flipbook.RandomStartFrame = true;
    ps->TemplateProps.rotation = 1.25f;
    ps->TemplateProps.randomRotation = false;

    Engine::SceneSerializer srcSerializer(srcScene);
    const std::string serialized = srcSerializer.SerializeToString();

    auto dstScene = std::make_shared<Engine::Scene>("VFX01DeserializerTest");
    Engine::SceneSerializer dstSerializer(dstScene);
    ASSERT_TRUE(dstSerializer.DeserializeFromString(serialized));

    Engine::GameObject* restoredGo = nullptr;
    for (const auto& obj : dstScene->GetGameObjects()) {
        if (obj && obj->GetName() == "SplinterEmitter") {
            restoredGo = obj.get();
            break;
        }
    }
    ASSERT_NE(restoredGo, nullptr);
    auto* restoredPs = restoredGo->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(restoredPs, nullptr);

    EXPECT_EQ(restoredPs->GetParticles().size(), 256u);
    EXPECT_EQ(restoredPs->Orientation, Engine::ParticleOrientation::FlatGroundXZ);
    EXPECT_FLOAT_EQ(restoredPs->GravityY, -18.0f);
    EXPECT_FLOAT_EQ(restoredPs->LinearDrag, 2.5f);
    EXPECT_TRUE(restoredPs->CollideGroundY);
    EXPECT_FLOAT_EQ(restoredPs->GroundPlaneY, 0.03f);
    EXPECT_FLOAT_EQ(restoredPs->GroundBounceRestitution, 0.45f);
    EXPECT_TRUE(restoredPs->Flipbook.Enabled);
    EXPECT_EQ(restoredPs->Flipbook.Columns, 4u);
    EXPECT_EQ(restoredPs->Flipbook.Rows, 3u);
    EXPECT_EQ(restoredPs->Flipbook.TotalFrames, 12u);
    EXPECT_FLOAT_EQ(restoredPs->Flipbook.CyclesOverLifetime, 2.5f);
    EXPECT_TRUE(restoredPs->Flipbook.RandomStartFrame);
    EXPECT_FLOAT_EQ(restoredPs->TemplateProps.rotation, 1.25f);
    EXPECT_FALSE(restoredPs->TemplateProps.randomRotation);
}

} // namespace

