#include <gtest/gtest.h>
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Scene/Components/FoliageWindComponents.h"

#include <box2d/box2d.h>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {

TEST(PrefabAndPoolingTest, PrefabSerializationAndFileRoundTrip) {
    auto scene = std::make_shared<Engine::Scene>("SourceScene");

    // Create 3-level / multi-child hierarchy: Brazier (root) -> PointLight (child 1) + Flames (child 2)
    auto* brazier = scene->CreateGameObject("RitualBrazier");
    brazier->SetTag(Engine::EntityTag::SafeZoneBrazier);
    brazier->SetLayer(Engine::EntityLayer::Environment);
    brazier->GetTransform()->position = { 10.0f, 5.0f, 20.0f };

    auto* sprite = brazier->AddComponent<Engine::SpriteRenderer>();
    sprite->SetAlphaCutoff(0.25f);
    sprite->SetColor({ 1.0f, 0.9f, 0.8f, 1.0f });

    auto* lightGo = scene->CreateGameObject("BrazierLight");
    lightGo->GetTransform()->position = { 0.0f, 1.2f, 0.0f };
    lightGo->GetTransform()->SetParent(brazier->GetTransform(), /*keepWorldTransform=*/false);
    auto* light = lightGo->AddComponent<Engine::PointLightComponent>();
    light->Color = { 1.0f, 0.6f, 0.2f };
    light->Intensity = 3.5f;
    light->Radius = 6.0f;

    auto* flamesGo = scene->CreateGameObject("BrazierFlames");
    flamesGo->GetTransform()->position = { 0.0f, 0.9f, 0.0f };
    flamesGo->GetTransform()->SetParent(brazier->GetTransform(), /*keepWorldTransform=*/false);
    auto* ps = flamesGo->AddComponent<Engine::ParticleSystem>(50u);
    ps->AutoEmit = true;
    ps->EmitRate = 15.0f;

    // Snapshot to Prefab asset (normalizing root position to 0,0,0)
    auto prefab = Engine::Prefab::CreateFromGameObject(brazier, /*zeroRootPosition=*/true);
    ASSERT_NE(prefab, nullptr);
    EXPECT_TRUE(prefab->IsValid());

    const auto& json = prefab->GetJson();
    EXPECT_EQ(json["Format"], "EnginePrefab");
    EXPECT_EQ(json["Version"], 1);
    ASSERT_TRUE(json.contains("Entities"));
    ASSERT_EQ(json["Entities"].size(), 3u);

    // Verify root position was zeroed in prefab asset
    const auto& rootEntityJson = json["Entities"][0];
    EXPECT_EQ(rootEntityJson["Name"], "RitualBrazier");
    EXPECT_EQ(rootEntityJson["ParentUUID"], 0ULL);
    EXPECT_EQ(rootEntityJson["Components"]["Transform"]["Position"][0], 0.0f);
    EXPECT_EQ(rootEntityJson["Components"]["Transform"]["Position"][1], 0.0f);
    EXPECT_EQ(rootEntityJson["Components"]["Transform"]["Position"][2], 0.0f);

    // Save to an isolated temp path so test runs never dirty tracked assets/test_brazier.prefab
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_prefab_roundtrip_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    struct TempDirCleanup {
        std::filesystem::path dir;
        ~TempDirCleanup() {
            std::error_code cleanupEc;
            std::filesystem::remove_all(dir, cleanupEc);
        }
    } cleanupGuard{ tempDir };

    const std::string testPath = (tempDir / "test_brazier.prefab").string();
    EXPECT_TRUE(prefab->SaveToFile(testPath));
    EXPECT_TRUE(std::filesystem::exists(testPath, ec));

    // Load back from disk
    auto loadedPrefab = Engine::Prefab::LoadFromFile(testPath);
    ASSERT_NE(loadedPrefab, nullptr);
    EXPECT_TRUE(loadedPrefab->IsValid());
    EXPECT_EQ(loadedPrefab->GetJson()["Entities"].size(), 3u);
}

TEST(PrefabAndPoolingTest, InstantiatePrefabUUIDRemappingAndOffsets) {
    auto srcScene = std::make_shared<Engine::Scene>("SrcScene");
    auto* rootGo = srcScene->CreateGameObject("ParentEntity");
    auto* childGo = srcScene->CreateGameObject("ChildEntity");
    childGo->GetTransform()->position = { 0.0f, 2.5f, 0.0f };
    childGo->GetTransform()->SetParent(rootGo->GetTransform(), false);

    auto prefab = Engine::Prefab::CreateFromGameObject(rootGo, true);
    ASSERT_NE(prefab, nullptr);

    auto dstScene = std::make_shared<Engine::Scene>("DstScene");

    // Instantiate two distinct instances
    auto* inst1 = dstScene->InstantiatePrefab(*prefab, glm::vec3(5.0f, 0.0f, 5.0f));
    auto* inst2 = dstScene->InstantiatePrefab(*prefab, glm::vec3(-10.0f, 0.0f, 20.0f));

    ASSERT_NE(inst1, nullptr);
    ASSERT_NE(inst2, nullptr);

    // Instances must be distinct objects with fresh, unique UUIDs
    EXPECT_NE(inst1, inst2);
    EXPECT_NE(inst1->GetUUID(), inst2->GetUUID());
    EXPECT_NE(inst1->GetUUID(), rootGo->GetUUID());
    EXPECT_NE(inst2->GetUUID(), rootGo->GetUUID());

    // Both instances must have exactly 1 child
    ASSERT_EQ(inst1->GetTransform()->GetChildren().size(), 1u);
    ASSERT_EQ(inst2->GetTransform()->GetChildren().size(), 1u);

    auto* child1 = inst1->GetTransform()->GetChildren()[0]->GetGameObject();
    auto* child2 = inst2->GetTransform()->GetChildren()[0]->GetGameObject();

    ASSERT_NE(child1, nullptr);
    ASSERT_NE(child2, nullptr);

    // Children must have unique UUIDs and correct parent links
    EXPECT_NE(child1->GetUUID(), child2->GetUUID());
    EXPECT_NE(child1->GetUUID(), childGo->GetUUID());
    EXPECT_NE(child2->GetUUID(), childGo->GetUUID());

    EXPECT_EQ(child1->GetTransform()->GetParent(), inst1->GetTransform());
    EXPECT_EQ(child2->GetTransform()->GetParent(), inst2->GetTransform());

    // World position verification
    const glm::vec3 p1 = inst1->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(p1.x, 5.0f, 1e-4f);
    EXPECT_NEAR(p1.y, 0.0f, 1e-4f);
    EXPECT_NEAR(p1.z, 5.0f, 1e-4f);

    const glm::vec3 cp1 = child1->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(cp1.x, 5.0f, 1e-4f);
    EXPECT_NEAR(cp1.y, 2.5f, 1e-4f);
    EXPECT_NEAR(cp1.z, 5.0f, 1e-4f);

    const glm::vec3 p2 = inst2->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(p2.x, -10.0f, 1e-4f);
    EXPECT_NEAR(p2.y, 0.0f, 1e-4f);
    EXPECT_NEAR(p2.z, 20.0f, 1e-4f);

    const glm::vec3 cp2 = child2->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(cp2.x, -10.0f, 1e-4f);
    EXPECT_NEAR(cp2.y, 2.5f, 1e-4f);
    EXPECT_NEAR(cp2.z, 20.0f, 1e-4f);
}

} // namespace
