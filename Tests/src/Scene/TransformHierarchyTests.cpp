#include <gtest/gtest.h>
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Components/SquashStretchComponent.h"
#include "Engine/Physics/Rigidbody2D.h"
#include "Engine/Physics/BoxCollider2D.h"
#include "Engine/Core/JobSystem.h"
#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <memory>
#include <vector>

namespace {

TEST(TransformHierarchyTest, LocalAndWorldMatrixComputations) {
    auto scene = std::make_shared<Engine::Scene>("TestScene");
    auto* go = scene->CreateGameObject("Root");
    auto* transform = go->GetTransform();

    transform->position = { 2.0f, 3.0f, 4.0f };
    transform->rotation = { 0.0f, 45.0f, 0.0f };
    transform->scale = { 2.0f, 2.0f, 2.0f };

    // For a root entity with no parent, World Transform == Local Transform
    EXPECT_EQ(transform->GetParent(), nullptr);
    EXPECT_TRUE(transform->GetChildren().empty());

    const glm::vec3 worldPos = transform->GetWorldPosition();
    EXPECT_NEAR(worldPos.x, 2.0f, 1e-4f);
    EXPECT_NEAR(worldPos.y, 3.0f, 1e-4f);
    EXPECT_NEAR(worldPos.z, 4.0f, 1e-4f);

    const glm::vec3 worldScale = transform->GetWorldScale();
    EXPECT_NEAR(worldScale.x, 2.0f, 1e-4f);
    EXPECT_NEAR(worldScale.y, 2.0f, 1e-4f);
    EXPECT_NEAR(worldScale.z, 2.0f, 1e-4f);

    const glm::vec3 worldRot = transform->GetWorldRotation();
    EXPECT_NEAR(worldRot.x, 0.0f, 1e-3f);
    EXPECT_NEAR(worldRot.y, 45.0f, 1e-3f);
    EXPECT_NEAR(worldRot.z, 0.0f, 1e-3f);

    const glm::mat4 localMat = transform->GetLocalMatrix();
    const glm::mat4 worldMat = transform->GetWorldMatrix();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(localMat[col][row], worldMat[col][row], 1e-4f);
        }
    }
}

TEST(TransformHierarchyTest, ThreeLevelHierarchyPositionAndRotationPropagation) {
    auto scene = std::make_shared<Engine::Scene>("HierarchyScene");

    auto* heroGo = scene->CreateGameObject("Hero");
    auto* handGo = scene->CreateGameObject("HandSocket");
    auto* flameGo = scene->CreateGameObject("TorchFlame");

    auto* heroTr = heroGo->GetTransform();
    auto* handTr = handGo->GetTransform();
    auto* flameTr = flameGo->GetTransform();

    // Setup initial local values
    heroTr->position = { 10.0f, 0.0f, 5.0f };
    heroTr->rotation = { 0.0f, 90.0f, 0.0f };
    heroTr->scale = { 1.0f, 1.0f, 1.0f };

    handTr->position = { 1.0f, 1.0f, 0.0f };
    handTr->rotation = { 0.0f, 0.0f, 0.0f };
    handTr->scale = { 1.0f, 1.0f, 1.0f };

    flameTr->position = { 0.0f, 0.5f, 0.0f };
    flameTr->rotation = { 0.0f, 0.0f, 0.0f };
    flameTr->scale = { 1.0f, 1.0f, 1.0f };

    // Reparent with keepWorldTransform = false to establish relative hierarchy offsets
    EXPECT_TRUE(handTr->SetParent(heroTr, /*keepWorldTransform=*/false));
    EXPECT_TRUE(flameTr->SetParent(handTr, /*keepWorldTransform=*/false));

    EXPECT_EQ(handTr->GetParent(), heroTr);
    EXPECT_EQ(flameTr->GetParent(), handTr);
    EXPECT_EQ(heroTr->GetChildren().size(), 1u);
    EXPECT_EQ(heroTr->GetChildren()[0], handTr);
    EXPECT_EQ(handTr->GetChildren().size(), 1u);
    EXPECT_EQ(handTr->GetChildren()[0], flameTr);

    // Expected transformation matrix: M_hero * M_hand * M_flame
    const glm::mat4 expectedWorldMat = heroTr->GetLocalMatrix() * handTr->GetLocalMatrix() * flameTr->GetLocalMatrix();
    const glm::vec4 expectedPos4 = expectedWorldMat * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 flameWorldPos = flameTr->GetWorldPosition();

    EXPECT_NEAR(flameWorldPos.x, expectedPos4.x, 1e-4f);
    EXPECT_NEAR(flameWorldPos.y, expectedPos4.y, 1e-4f);
    EXPECT_NEAR(flameWorldPos.z, expectedPos4.z, 1e-4f);

    // Modify Hero position directly via struct field (testing automatic dirty detection)
    heroTr->position = { 20.0f, 5.0f, 15.0f };

    const glm::mat4 updatedExpectedMat = heroTr->GetLocalMatrix() * handTr->GetLocalMatrix() * flameTr->GetLocalMatrix();
    const glm::vec4 updatedExpectedPos4 = updatedExpectedMat * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 updatedFlamePos = flameTr->GetWorldPosition();

    EXPECT_NEAR(updatedFlamePos.x, updatedExpectedPos4.x, 1e-4f);
    EXPECT_NEAR(updatedFlamePos.y, updatedExpectedPos4.y, 1e-4f);
    EXPECT_NEAR(updatedFlamePos.z, updatedExpectedPos4.z, 1e-4f);
}

TEST(TransformHierarchyTest, CycleRejection) {
    auto scene = std::make_shared<Engine::Scene>("CycleScene");

    auto* a = scene->CreateGameObject("A")->GetTransform();
    auto* b = scene->CreateGameObject("B")->GetTransform();
    auto* c = scene->CreateGameObject("C")->GetTransform();

    EXPECT_TRUE(b->SetParent(a));
    EXPECT_TRUE(c->SetParent(b));

    // A is ancestor of B and C
    EXPECT_TRUE(b->IsDescendantOf(a));
    EXPECT_TRUE(c->IsDescendantOf(a));
    EXPECT_TRUE(c->IsDescendantOf(b));
    EXPECT_FALSE(a->IsDescendantOf(c));

    // Self-parenting must be rejected
    EXPECT_FALSE(a->SetParent(a));
    EXPECT_EQ(a->GetParent(), nullptr);

    // Cycle: setting A's parent to B or C must be rejected
    EXPECT_FALSE(a->SetParent(b));
    EXPECT_EQ(a->GetParent(), nullptr);

    EXPECT_FALSE(a->SetParent(c));
    EXPECT_EQ(a->GetParent(), nullptr);

    // Hierarchy remains intact
    EXPECT_EQ(b->GetParent(), a);
    EXPECT_EQ(c->GetParent(), b);
}

TEST(TransformHierarchyTest, ReparentingWithKeepWorldTransform) {
    auto scene = std::make_shared<Engine::Scene>("ReparentScene");

    auto* parentGo = scene->CreateGameObject("Parent");
    auto* childGo = scene->CreateGameObject("Child");

    auto* parentTr = parentGo->GetTransform();
    auto* childTr = childGo->GetTransform();

    parentTr->position = { 10.0f, 0.0f, 0.0f };
    childTr->position = { 15.0f, 2.0f, 0.0f };

    const glm::vec3 originalChildWorldPos = childTr->GetWorldPosition();

    // Attach with keepWorldTransform = true
    EXPECT_TRUE(childTr->SetParent(parentTr, /*keepWorldTransform=*/true));

    // World position must be strictly preserved
    const glm::vec3 postParentWorldPos = childTr->GetWorldPosition();
    EXPECT_NEAR(postParentWorldPos.x, originalChildWorldPos.x, 1e-4f);
    EXPECT_NEAR(postParentWorldPos.y, originalChildWorldPos.y, 1e-4f);
    EXPECT_NEAR(postParentWorldPos.z, originalChildWorldPos.z, 1e-4f);

    // Local position should now be (5, 2, 0)
    EXPECT_NEAR(childTr->position.x, 5.0f, 1e-4f);
    EXPECT_NEAR(childTr->position.y, 2.0f, 1e-4f);
    EXPECT_NEAR(childTr->position.z, 0.0f, 1e-4f);

    // Moving parent propagates to child's world position
    parentTr->position = { 20.0f, 0.0f, 0.0f };
    EXPECT_NEAR(childTr->GetWorldPosition().x, 25.0f, 1e-4f);

    // Detach with keepWorldTransform = true
    EXPECT_TRUE(childTr->SetParent(nullptr, /*keepWorldTransform=*/true));
    EXPECT_EQ(childTr->GetParent(), nullptr);
    EXPECT_NEAR(childTr->GetWorldPosition().x, 25.0f, 1e-4f);
    EXPECT_NEAR(childTr->position.x, 25.0f, 1e-4f);

    // Moving former parent no longer affects child
    parentTr->position = { 100.0f, 0.0f, 0.0f };
    EXPECT_NEAR(childTr->GetWorldPosition().x, 25.0f, 1e-4f);
}

TEST(TransformHierarchyTest, CascadingDestroyGameObject) {
    auto scene = std::make_shared<Engine::Scene>("DestroyScene");

    auto* root = scene->CreateGameObject("Root");
    auto* child = scene->CreateGameObject("Child");
    auto* grandChild = scene->CreateGameObject("GrandChild");

    child->GetTransform()->SetParent(root->GetTransform(), false);
    grandChild->GetTransform()->SetParent(child->GetTransform(), false);

    EXPECT_EQ(scene->GetGameObjects().size(), 3u);

    // 1. When iterating entities mid-frame, destroying root cascades pending destroy to all descendants
    scene->SetIsIteratingEntities(true);
    scene->DestroyGameObject(root);

    EXPECT_TRUE(root->IsPendingDestroy());
    EXPECT_TRUE(child->IsPendingDestroy());
    EXPECT_TRUE(grandChild->IsPendingDestroy());

    // Flush lifecycle queues at end of frame
    scene->SetIsIteratingEntities(false);
    scene->FlushEntityLifecycleQueues();

    EXPECT_EQ(scene->GetGameObjects().size(), 0u);

    // 2. Direct immediate destruction outside frame iteration
    auto* root2 = scene->CreateGameObject("Root2");
    auto* child2 = scene->CreateGameObject("Child2");
    child2->GetTransform()->SetParent(root2->GetTransform(), false);
    EXPECT_EQ(scene->GetGameObjects().size(), 2u);

    scene->DestroyGameObject(root2);
    EXPECT_EQ(scene->GetGameObjects().size(), 0u);
}

TEST(TransformHierarchyTest, ActiveInHierarchyInheritance) {
    auto scene = std::make_shared<Engine::Scene>("ActiveScene");

    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child");

    child->GetTransform()->SetParent(parent->GetTransform(), false);

    // Initially both are active
    EXPECT_TRUE(parent->IsActive());
    EXPECT_TRUE(parent->IsActiveInHierarchy());
    EXPECT_TRUE(child->IsActive());
    EXPECT_TRUE(child->IsActiveInHierarchy());

    // Deactivating parent deactivates child in hierarchy, but leaves child's local active flag true
    parent->SetActive(false);

    EXPECT_FALSE(parent->IsActive());
    EXPECT_FALSE(parent->IsActiveInHierarchy());
    EXPECT_TRUE(child->IsActive()); // Local flag is still true
    EXPECT_FALSE(child->IsActiveInHierarchy()); // Hierarchy flag is false

    // Reactivating parent restores child's hierarchy active state
    parent->SetActive(true);
    EXPECT_TRUE(child->IsActiveInHierarchy());
}

TEST(TransformHierarchyTest, SceneSerializerParentChildRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("SerializerTestScene");

    auto* hero = srcScene->CreateGameObjectWithUUID(1001ULL, "Hero");
    auto* hand = srcScene->CreateGameObjectWithUUID(1002ULL, "HandSocket");
    auto* sword = srcScene->CreateGameObjectWithUUID(1003ULL, "Sword");

    hero->GetTransform()->position = { 10.0f, 20.0f, 30.0f };
    hand->GetTransform()->position = { 1.0f, 0.0f, 0.0f };
    sword->GetTransform()->position = { 0.0f, 2.0f, 0.0f };

    EXPECT_TRUE(hand->GetTransform()->SetParent(hero->GetTransform(), /*keepWorldTransform=*/false));
    EXPECT_TRUE(sword->GetTransform()->SetParent(hand->GetTransform(), /*keepWorldTransform=*/false));

    Engine::SceneSerializer serializer(srcScene);
    const std::string jsonString = serializer.SerializeToString();
    EXPECT_FALSE(jsonString.empty());

    // Deserialize into destination scene
    auto dstScene = std::make_shared<Engine::Scene>("DeserializedScene");
    Engine::SceneSerializer deserializer(dstScene);
    EXPECT_TRUE(deserializer.DeserializeFromString(jsonString));

    EXPECT_EQ(dstScene->GetGameObjects().size(), 3u);

    auto* dstHero = dstScene->FindGameObjectByUUID(1001ULL);
    auto* dstHand = dstScene->FindGameObjectByUUID(1002ULL);
    auto* dstSword = dstScene->FindGameObjectByUUID(1003ULL);

    ASSERT_NE(dstHero, nullptr);
    ASSERT_NE(dstHand, nullptr);
    ASSERT_NE(dstSword, nullptr);

    // Verify parent links
    EXPECT_EQ(dstHero->GetTransform()->GetParent(), nullptr);
    EXPECT_EQ(dstHand->GetTransform()->GetParent(), dstHero->GetTransform());
    EXPECT_EQ(dstSword->GetTransform()->GetParent(), dstHand->GetTransform());

    // Verify children lists
    ASSERT_EQ(dstHero->GetTransform()->GetChildren().size(), 1u);
    EXPECT_EQ(dstHero->GetTransform()->GetChildren()[0], dstHand->GetTransform());

    ASSERT_EQ(dstHand->GetTransform()->GetChildren().size(), 1u);
    EXPECT_EQ(dstHand->GetTransform()->GetChildren()[0], dstSword->GetTransform());

    // Verify world position computation matches
    const glm::vec3 dstSwordWorldPos = dstSword->GetTransform()->GetWorldPosition();
    EXPECT_NEAR(dstSwordWorldPos.x, 11.0f, 1e-4f);
    EXPECT_NEAR(dstSwordWorldPos.y, 22.0f, 1e-4f);
    EXPECT_NEAR(dstSwordWorldPos.z, 30.0f, 1e-4f);
}

TEST(TransformHierarchyTest, SetWorldRotationWithRotatedParent) {
    auto scene = std::make_shared<Engine::Scene>("RotationCompositionScene");
    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child");

    auto* pTr = parent->GetTransform();
    auto* cTr = child->GetTransform();
    pTr->position = { 3.0f, -2.0f, 5.0f };
    pTr->rotation = { 35.0f, -60.0f, 20.0f };
    pTr->scale = { 2.0f, 2.0f, 2.0f };
    cTr->position = { 1.0f, 0.5f, -0.5f };

    ASSERT_TRUE(cTr->SetParent(pTr, false));

    const glm::vec3 targetWorldRot = { -15.0f, 75.0f, 30.0f };
    cTr->SetWorldRotation(targetWorldRot);

    const glm::mat4 worldMat = cTr->GetWorldMatrix();
    const glm::mat4 expectedRot =
        glm::rotate(glm::mat4(1.0f), glm::radians(targetWorldRot.x), glm::vec3(1, 0, 0)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(targetWorldRot.y), glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(targetWorldRot.z), glm::vec3(0, 0, 1));

    for (int col = 0; col < 3; ++col) {
        const glm::vec3 actualAxis = glm::normalize(glm::vec3(worldMat[col]));
        const glm::vec3 expectedAxis = glm::vec3(expectedRot[col]);
        EXPECT_NEAR(actualAxis.x, expectedAxis.x, 1e-4f);
        EXPECT_NEAR(actualAxis.y, expectedAxis.y, 1e-4f);
        EXPECT_NEAR(actualAxis.z, expectedAxis.z, 1e-4f);
    }
}

TEST(TransformHierarchyTest, ConcurrentGetWorldMatrixReadsAreRaceFree) {
    Engine::JobSystem::Init(4);

    auto scene = std::make_shared<Engine::Scene>("ConcurrentTransformScene");
    auto* root = scene->CreateGameObject("Root");
    auto* arm = scene->CreateGameObject("Arm");
    auto* wrist = scene->CreateGameObject("Wrist");
    auto* weapon = scene->CreateGameObject("Weapon");

    auto* rootTr = root->GetTransform();
    auto* armTr = arm->GetTransform();
    auto* wristTr = wrist->GetTransform();
    auto* weaponTr = weapon->GetTransform();

    ASSERT_TRUE(armTr->SetParent(rootTr, false));
    ASSERT_TRUE(wristTr->SetParent(armTr, false));
    ASSERT_TRUE(weaponTr->SetParent(wristTr, false));

    rootTr->position = { 4.0f, 1.0f, -3.0f };
    rootTr->rotation = { 0.0f, 90.0f, 0.0f };
    armTr->position = { 2.0f, 0.0f, 0.0f };
    wristTr->position = { 0.0f, 3.0f, 0.0f };
    weaponTr->position = { 0.0f, 0.0f, 1.5f };

    // Reference world position on single thread first? Test cold dirty cache across parallel workers!
    constexpr size_t kWorkers = 64;
    std::vector<glm::vec3> positions(kWorkers, glm::vec3(0.0f));

    Engine::JobSystem::ParallelFor(kWorkers, [&](size_t i) {
        const glm::mat4 mat = weaponTr->GetWorldMatrix();
        positions[i] = glm::vec3(mat[3]);
    });

    const glm::vec3 expected = weaponTr->GetWorldPosition();
    for (size_t i = 0; i < kWorkers; ++i) {
        EXPECT_NEAR(positions[i].x, expected.x, 1e-4f);
        EXPECT_NEAR(positions[i].y, expected.y, 1e-4f);
        EXPECT_NEAR(positions[i].z, expected.z, 1e-4f);
    }

    Engine::JobSystem::Shutdown();
}

namespace {

class ActiveHierarchyTracker final : public Engine::Component {
public:
    int enableCount = 0;
    int disableCount = 0;

    void OnEnable() override { ++enableCount; }
    void OnDisable() override { ++disableCount; }
};

} // namespace

TEST(TransformHierarchyTest, ReparentingSyncsActiveInHierarchyAndRigidbody2D) {
    Engine::Scene scene("ReparentActiveScene");
    scene.OnRuntimeStart();

    auto* parent = scene.CreateGameObject("Parent");
    auto* child = scene.CreateGameObject("Child");
    auto* rb2d = child->AddComponent<Engine::Rigidbody2D>();
    auto* bc2d = child->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 1.0f, 1.0f };

    b2BodyId bodyId = b2LoadBodyId(rb2d->RuntimeBody.Value);
    ASSERT_TRUE(b2Body_IsValid(bodyId));
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));
    EXPECT_TRUE(child->IsActiveInHierarchy());

    // Deactivate parent while child is still active at root
    parent->SetActive(false);
    EXPECT_FALSE(parent->IsActiveInHierarchy());
    EXPECT_TRUE(child->IsActiveInHierarchy());
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    // Reparent active child under inactive parent: must transition to inactive and disable Box2D body
    EXPECT_TRUE(child->GetTransform()->SetParent(parent->GetTransform(), false));
    EXPECT_FALSE(child->IsActiveInHierarchy());
    EXPECT_FALSE(b2Body_IsEnabled(bodyId));

    // Reparent child back to root: must transition back to active and re-enable Box2D body
    EXPECT_TRUE(child->GetTransform()->SetParent(nullptr, false));
    EXPECT_TRUE(child->IsActiveInHierarchy());
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    scene.OnRuntimeStop();
}

TEST(TransformHierarchyTest, SetActiveUnderInactiveParentFiresZeroSpuriousCallbacks) {
    auto scene = std::make_shared<Engine::Scene>("HierarchyCallbacksScene");
    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child");

    auto* tracker = child->AddComponent<ActiveHierarchyTracker>();
    EXPECT_EQ(tracker->enableCount, 1);
    EXPECT_EQ(tracker->disableCount, 0);

    // Parent child to parent
    EXPECT_TRUE(child->GetTransform()->SetParent(parent->GetTransform(), false));

    // Deactivate parent -> child transitions to inactive in hierarchy, OnDisable() fires once
    parent->SetActive(false);
    EXPECT_FALSE(child->IsActiveInHierarchy());
    EXPECT_EQ(tracker->enableCount, 1);
    EXPECT_EQ(tracker->disableCount, 1);

    // Under inactive parent: child->SetActive(false) should NOT fire duplicate OnDisable()
    child->SetActive(false);
    EXPECT_FALSE(child->IsActiveInHierarchy());
    EXPECT_EQ(tracker->enableCount, 1);
    EXPECT_EQ(tracker->disableCount, 1);

    // Under inactive parent: child->SetActive(true) should NOT fire premature OnEnable() or spurious OnDisable()
    child->SetActive(true);
    EXPECT_FALSE(child->IsActiveInHierarchy());
    EXPECT_EQ(tracker->enableCount, 1);
    EXPECT_EQ(tracker->disableCount, 1);

    // Now activating parent: child (which had SetActive(true)) becomes active in hierarchy and receives OnEnable()
    parent->SetActive(true);
    EXPECT_TRUE(child->IsActiveInHierarchy());
    EXPECT_EQ(tracker->enableCount, 2);
    EXPECT_EQ(tracker->disableCount, 1);
}

TEST(TransformHierarchyTest, ZeroScaleParentInversionNeverProducesNaN) {
    auto scene = std::make_shared<Engine::Scene>("ZeroScaleScene");
    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child");

    auto* parentTr = parent->GetTransform();
    auto* childTr = child->GetTransform();

    // 1. Test parent with complete zero scale (0, 0, 0)
    parentTr->position = { 5.0f, 10.0f, 15.0f };
    parentTr->rotation = { 0.0f, 45.0f, 0.0f };
    parentTr->scale = { 0.0f, 0.0f, 0.0f };

    childTr->position = { 10.0f, 20.0f, 30.0f };
    childTr->rotation = { 0.0f, 0.0f, 0.0f };
    childTr->scale = { 1.0f, 1.0f, 1.0f };

    EXPECT_TRUE(childTr->SetParent(parentTr, /*keepWorldTransform=*/true));

    EXPECT_FALSE(std::isnan(childTr->position.x));
    EXPECT_FALSE(std::isnan(childTr->position.y));
    EXPECT_FALSE(std::isnan(childTr->position.z));
    EXPECT_FALSE(std::isinf(childTr->position.x));
    EXPECT_FALSE(std::isinf(childTr->position.y));
    EXPECT_FALSE(std::isinf(childTr->position.z));

    EXPECT_FALSE(std::isnan(childTr->rotation.x));
    EXPECT_FALSE(std::isnan(childTr->rotation.y));
    EXPECT_FALSE(std::isnan(childTr->rotation.z));

    EXPECT_FALSE(std::isnan(childTr->scale.x));
    EXPECT_FALSE(std::isnan(childTr->scale.y));
    EXPECT_FALSE(std::isnan(childTr->scale.z));

    // SetWorldPosition with zero-scale parent
    childTr->SetWorldPosition({ 25.0f, 30.0f, 35.0f });
    EXPECT_FALSE(std::isnan(childTr->position.x));
    EXPECT_FALSE(std::isnan(childTr->position.y));
    EXPECT_FALSE(std::isnan(childTr->position.z));
    EXPECT_FALSE(std::isinf(childTr->position.x));

    // 2. Test parent with single zero axis (e.g. scale.y = 0)
    parentTr->scale = { 2.0f, 0.0f, 2.0f };
    childTr->SetWorldPosition({ 15.0f, 10.0f, 25.0f });
    EXPECT_FALSE(std::isnan(childTr->position.x));
    EXPECT_FALSE(std::isnan(childTr->position.y));
    EXPECT_FALSE(std::isnan(childTr->position.z));

    // Direct matrix test for SafeInvertMatrix
    glm::mat4 zeroScaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(0.0f));
    glm::mat4 safeInv = Engine::Transform::SafeInvertMatrix(zeroScaleMat);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_FALSE(std::isnan(safeInv[col][row]));
            EXPECT_FALSE(std::isinf(safeInv[col][row]));
        }
    }
}

TEST(TransformHierarchyTest, SquashStretchTracksWorldPositionAndToleratesLagSpike) {
    auto scene = std::make_shared<Engine::Scene>("SquashWorldPosScene");
    auto* parent = scene->CreateGameObject("Parent");
    auto* child = scene->CreateGameObject("Child");

    EXPECT_TRUE(child->GetTransform()->SetParent(parent->GetTransform(), false));
    auto* squash = child->AddComponent<Engine::SquashStretchComponent>();
    squash->EnableWalkHop = false; // Isolate vertical deformations

    // Initial frame seeds m_LastPosition
    child->Update(0.016f);

    // Move parent upward in world space: child local pos remains (0,0,0)
    parent->GetTransform()->position = { 0.0f, 2.0f, 0.0f };
    child->Update(0.016f);

    // Child should detect jump launch based on world position change!
    glm::vec2 scaleMult = squash->GetScaleMultiplier();
    EXPECT_GT(scaleMult.y, 1.0f); // Stretch in Y

    // Reset squash component
    squash->Reset();
    child->Update(0.016f);

    // Lag spike test: 500ms frame delta with small physical displacement (0.05m upward => speed = 0.1 m/s)
    parent->GetTransform()->position = { 0.0f, 2.05f, 0.0f };
    child->Update(0.50f);

    // Because actual deltaTime is used for velocity, verticalSpeed is 0.05 / 0.50 = 0.10 m/s
    // (below the 0.25 m/s jump launch threshold), NOT 0.05 / 0.05 = 1.0 m/s!
    scaleMult = squash->GetScaleMultiplier();
    EXPECT_NEAR(scaleMult.y, 1.0f, 0.05f); // No false-positive giant jump stretch
}

} // namespace
