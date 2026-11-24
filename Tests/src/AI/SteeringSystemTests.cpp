#include <gtest/gtest.h>
#include "Engine/AI/SteeringSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Physics/PhysicsLayer.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Terrain/TerrainChunkManager.h"

#include <cmath>
#include <vector>

namespace {

TEST(SteeringSystemTest, PolarDirectionsAndSlotInitialization) {
    const auto& dirs = Engine::SteeringSystem::GetSlotDirections();
    ASSERT_EQ(dirs.size(), 12u);

    // Each vector should have unit length
    for (size_t i = 0; i < dirs.size(); ++i) {
        EXPECT_NEAR(glm::length(dirs[i]), 1.0f, 1e-5f);
    }

    // Direction 0: (+X) (1, 0)
    EXPECT_NEAR(dirs[0].x, 1.0f, 1e-5f);
    EXPECT_NEAR(dirs[0].y, 0.0f, 1e-5f);

    // Direction 3: (+Z) (0, 1)
    EXPECT_NEAR(dirs[3].x, 0.0f, 1e-5f);
    EXPECT_NEAR(dirs[3].y, 1.0f, 1e-5f);

    // Direction 6: (-X) (-1, 0)
    EXPECT_NEAR(dirs[6].x, -1.0f, 1e-5f);
    EXPECT_NEAR(dirs[6].y, 0.0f, 1e-5f);

    // Direction 9: (-Z) (0, -1)
    EXPECT_NEAR(dirs[9].x, 0.0f, 1e-5f);
    EXPECT_NEAR(dirs[9].y, -1.0f, 1e-5f);

    // Dot product between adjacent 30-degree slots should be cos(30 deg) ~= 0.866025
    for (size_t i = 0; i < dirs.size(); ++i) {
        const size_t next = (i + 1) % dirs.size();
        EXPECT_NEAR(glm::dot(dirs[i], dirs[next]), std::cos(30.0f * 3.14159265f / 180.0f), 1e-4f);
    }
}

TEST(SteeringSystemTest, SeekStraightUnobstructed) {
    Engine::Scene scene("EmptyTestScene");
    Engine::SteeringParams params;
    params.MaxSpeed = 4.2f;
    params.ArrivalRadius = 0.5f;

    const glm::vec2 agentPos{ 0.0f, 10.0f };
    const glm::vec2 targetPos{ 0.0f, 0.0f };

    std::array<float, 12> interest{};
    std::array<float, 12> danger{};
    std::array<float, 12> context{};

    const glm::vec2 vel = Engine::SteeringSystem::EvaluateContextMap(
        scene, agentPos, 0.35f, targetPos, {}, params, nullptr,
        &interest, &danger, &context);

    // In unobstructed space, danger should be all zero
    for (float d : danger) {
        EXPECT_FLOAT_EQ(d, 0.0f);
    }

    // Target is in direction (0, -1) which corresponds to slot 9
    EXPECT_GT(interest[9], 0.99f);
    EXPECT_GT(context[9], 0.99f);

    // Steering velocity should point straight down (-Z) at MaxSpeed
    EXPECT_NEAR(vel.x, 0.0f, 1e-3f);
    EXPECT_NEAR(vel.y, -4.2f, 1e-3f);
    EXPECT_NEAR(glm::length(vel), 4.2f, 1e-3f);
}

TEST(SteeringSystemTest, WispOrbitBehavior) {
    Engine::Scene scene("OrbitTestScene");
    Engine::SteeringParams params;
    params.MaxSpeed = 4.2f;
    params.TargetOrbitRadius = 3.0f; // 3m orbit radius
    params.OrbitClockwise = true;

    const glm::vec2 targetPos{ 0.0f, 0.0f };

    // 1. Agent on the orbit circle at (3.0, 0.0)
    // Tangent for clockwise orbit at (3, 0) is pointing towards (0, -1) (-Z)
    const glm::vec2 velOnRing = Engine::SteeringSystem::ComputeContextSteeringXZ(
        scene, glm::vec2(3.0f, 0.0f), 0.35f, targetPos, {}, params);

    EXPECT_NEAR(velOnRing.x, 0.0f, 0.3f);
    EXPECT_LT(velOnRing.y, -3.0f); // Strong tangential speed along -Z

    // 2. Agent far outside the orbit circle at (6.0, 0.0)
    // Needs inward radial pull (negative X)
    const glm::vec2 velOutside = Engine::SteeringSystem::ComputeContextSteeringXZ(
        scene, glm::vec2(6.0f, 0.0f), 0.35f, targetPos, {}, params);

    EXPECT_LT(velOutside.x, -0.5f); // Pulls inward toward target

    // 3. Agent inside the orbit circle at (1.5, 0.0)
    // Needs outward push (positive X)
    const glm::vec2 velInside = Engine::SteeringSystem::ComputeContextSteeringXZ(
        scene, glm::vec2(1.5f, 0.0f), 0.35f, targetPos, {}, params);

    EXPECT_GT(velInside.x, 0.5f); // Pushes outward toward radius
}

TEST(SteeringSystemTest, NeighborSeparationRepulsion) {
    Engine::Scene scene("SeparationScene");
    Engine::SteeringParams params;
    params.MaxSpeed = 4.0f;
    params.SeparationRadius = 1.5f;
    params.SeparationWeight = 2.0f;

    const glm::vec2 agentPos{ 0.0f, 10.0f };
    const glm::vec2 targetPos{ 0.0f, 0.0f };

    // Neighbor directly on the right at (0.6, 10.0)
    const std::vector<glm::vec2> neighbors = { glm::vec2(0.6f, 10.0f) };

    std::array<float, 12> interest{};
    std::array<float, 12> danger{};
    std::array<float, 12> context{};

    const glm::vec2 vel = Engine::SteeringSystem::EvaluateContextMap(
        scene, agentPos, 0.35f, targetPos, neighbors, params, nullptr,
        &interest, &danger, &context);

    // Slots pointing right (+X, around slot 0) should have high danger from neighbor
    EXPECT_GT(danger[0], 0.5f);

    // Steering velocity should be deflected away from neighbor (to the left, negative X)
    EXPECT_LT(vel.x, -0.1f);
    EXPECT_LT(vel.y, -1.0f); // Still moving towards target (-Z)
}

TEST(SteeringSystemTest, FireCellAvoidance) {
    auto scene = std::make_shared<Engine::Scene>("FireAvoidanceScene");
    auto* tcmGo = scene->CreateGameObject("ChunkManager");
    auto* mgr = tcmGo->AddComponent<Engine::TerrainChunkManager>(16.0f, 65u, 1, 2);

    // Make chunk (0, 0) editable and paint flammable tar at (0, 5)
    mgr->EnsureEditableChunk({ 0, 0 });
    mgr->PaintMaterialWorld(glm::vec2(0.0f, 5.0f), 2u /*Tar*/, 2.0f, 1.0f);

    // Ignite a fire cell at (0, 5) with radius 1.0m
    mgr->IgniteAtXZ(glm::vec2(0.0f, 5.0f), 1.0f);
    EXPECT_TRUE(scene->IsBurningAtXZ(0.0f, 5.0f));

    Engine::SteeringParams params;
    params.MaxSpeed = 4.0f;
    params.FireAvoidanceWeight = 3.5f;
    params.ObstacleWhiskerLength = 2.5f;

    const glm::vec2 agentPos{ 0.0f, 7.0f };
    const glm::vec2 targetPos{ 0.0f, 0.0f };

    std::array<float, 12> interest{};
    std::array<float, 12> danger{};
    std::array<float, 12> context{};

    const glm::vec2 vel = Engine::SteeringSystem::EvaluateContextMap(
        *scene, agentPos, 0.35f, targetPos, {}, params, nullptr,
        &interest, &danger, &context);

    // Slot 9 points straight ahead along -Z towards the fire at (0, 5)
    EXPECT_GT(danger[9], 2.0f);

    // Steering should deflect away from fire (diverging with non-zero X)
    EXPECT_NE(vel.x, 0.0f);
}

TEST(SteeringSystemTest, HordeTreeSplitAndReachTarget) {
    auto scene = std::make_shared<Engine::Scene>("HordeTreeTestScene");

    // 1. Place a static tree collider at (0, 5) directly between agents and target (0, 0)
    auto* tree = scene->CreateGameObject("PineTrunk");
    tree->GetTransform()->position = { 0.0f, 0.0f, 5.0f };
    auto* circleCol = tree->AddComponent<Engine::CircleCollider2D>();
    circleCol->Radius = 1.0f;
    circleCol->CategoryBits = Engine::PhysicsLayer::Environment;
    circleCol->MaskBits = Engine::PhysicsLayer::All;

    const glm::vec2 targetXZ{ 0.0f, 0.0f };

    Engine::SteeringParams params;
    params.MaxSpeed = 4.2f;
    params.SeparationRadius = 1.15f;
    params.SeparationWeight = 1.65f;
    params.ObstacleWhiskerLength = 1.8f;
    params.AvoidanceWeight = 2.5f;
    params.ArrivalRadius = 0.8f;

    // 2. 10 agents starting at Z = 10, distributed slightly across X
    struct Agent {
        glm::vec2 Pos{ 0.0f };
    };

    std::vector<Agent> agents(10);
    for (size_t i = 0; i < agents.size(); ++i) {
        // Distribute X across [-1.35, 1.35]
        const float x = -1.35f + static_cast<float>(i) * (2.70f / 9.0f);
        const float z = 10.0f + ((i % 2 == 0) ? 0.0f : 0.25f);
        agents[i].Pos = { x, z };
    }

    constexpr float dt = 1.0f / 60.0f;
    constexpr int totalFrames = 180;

    int leftSplitCount = 0;
    int rightSplitCount = 0;

    // Step 180 simulation frames
    for (int frame = 0; frame < totalFrames; ++frame) {
        // Collect neighbor positions snapshot
        std::vector<glm::vec2> allPositions(agents.size());
        for (size_t i = 0; i < agents.size(); ++i) {
            allPositions[i] = agents[i].Pos;
        }

        // Compute and apply context steering for each agent
        for (size_t i = 0; i < agents.size(); ++i) {
            // Build neighbor slice excluding self
            std::vector<glm::vec2> neighbors;
            neighbors.reserve(agents.size() - 1);
            for (size_t j = 0; j < agents.size(); ++j) {
                if (i != j) {
                    neighbors.push_back(allPositions[j]);
                }
            }

            const glm::vec2 steerVel = Engine::SteeringSystem::ComputeContextSteeringXZ(
                *scene, agents[i].Pos, 0.35f, targetXZ, neighbors, params);

            agents[i].Pos += steerVel * dt;

            // Track left vs right split when passing the tree at Z ~= 5
            if (std::abs(agents[i].Pos.y - 5.0f) < 0.5f) {
                if (agents[i].Pos.x < -0.2f) leftSplitCount++;
                if (agents[i].Pos.x > 0.2f) rightSplitCount++;
            }

            // Assert agent never penetrates tree collider (center at (0, 5), radius 1.0m, agent radius 0.35m)
            const float distToTree = glm::distance(agents[i].Pos, glm::vec2(0.0f, 5.0f));
            EXPECT_GE(distToTree, 0.90f) << "Agent " << i << " penetrated tree collider at frame " << frame;
        }
    }

    // Assert that agents split cleanly on both sides of the tree trunk
    EXPECT_GT(leftSplitCount, 0) << "Expected at least one agent to navigate around the left of the tree";
    EXPECT_GT(rightSplitCount, 0) << "Expected at least one agent to navigate around the right of the tree";

    // Assert all 10 agents reached destination target (0, 0) within encirclement radius
    for (size_t i = 0; i < agents.size(); ++i) {
        const float distToTarget = glm::distance(agents[i].Pos, targetXZ);
        EXPECT_LT(distToTarget, 2.0f) << "Agent " << i << " failed to reach target (dist=" << distToTarget << ")";
    }

    // Assert no two agents overlap (dist >= 0.70m per specification)
    for (size_t i = 0; i < agents.size(); ++i) {
        for (size_t j = i + 1; j < agents.size(); ++j) {
            const float mutualDist = glm::distance(agents[i].Pos, agents[j].Pos);
            EXPECT_GE(mutualDist, 0.70f)
                << "Agents " << i << " and " << j << " overlapped at end (dist=" << mutualDist << "m < 0.70m)";
        }
    }
}

} // namespace
