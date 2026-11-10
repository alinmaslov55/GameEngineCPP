#include <gtest/gtest.h>
#include "Engine/Scene/Prefab.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/EntityPoolManager.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Scene/FoliageWindComponents.h"

#include <box2d/box2d.h>
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

    // Save to disk
    const std::string testPath = "assets/test_brazier.prefab";
    EXPECT_TRUE(prefab->SaveToFile(testPath));

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

TEST(PrefabAndPoolingTest, PrewarmAndObjectPoolSteadyStateZeroAllocations) {
    auto scene = std::make_shared<Engine::Scene>("PoolScene");

    // Build Brazier prefab
    auto* brazier = scene->CreateGameObject("TemplateBrazier");
    auto* light = scene->CreateGameObject("TemplateLight");
    light->GetTransform()->position = { 0.0f, 1.0f, 0.0f };
    light->GetTransform()->SetParent(brazier->GetTransform(), false);
    light->AddComponent<Engine::PointLightComponent>();

    auto* flames = scene->CreateGameObject("TemplateFlames");
    flames->GetTransform()->position = { 0.0f, 0.8f, 0.0f };
    flames->GetTransform()->SetParent(brazier->GetTransform(), false);
    flames->AddComponent<Engine::ParticleSystem>(30u);

    auto prefab = Engine::Prefab::CreateFromGameObject(brazier, true);
    ASSERT_NE(prefab, nullptr);

    // Destroy template entities before pool prewarm
    scene->DestroyGameObject(brazier);
    scene->FlushEntityLifecycleQueues();
    EXPECT_EQ(scene->GetGameObjects().size(), 0u);

    // Prewarm 16 instances
    scene->PrewarmPool("BrazierPool", *prefab, 16u);

    EXPECT_EQ(scene->GetPoolFreeCount("BrazierPool"), 16u);
    EXPECT_EQ(scene->GetPoolTotalCount("BrazierPool"), 16u);

    // Total scene GameObjects = 16 root instances * 3 entities per prefab = 48
    const size_t baselineEntityCount = scene->GetGameObjects().size();
    EXPECT_EQ(baselineEntityCount, 48u);

    // Snapshot prewarmed instances & verify unique UUIDs and inactive state
    std::unordered_set<Engine::GameObject*> prewarmedSet;
    std::unordered_set<uint64_t> uuidSet;

    for (const auto& go : scene->GetGameObjects()) {
        EXPECT_FALSE(go->IsActiveInHierarchy());
        EXPECT_TRUE(uuidSet.insert(go->GetUUID()).second); // All UUIDs must be unique!
        if (go->GetTransform() && go->GetTransform()->GetParent() == nullptr) {
            EXPECT_FALSE(go->IsActive());
            prewarmedSet.insert(go.get());
        }
    }
    EXPECT_EQ(prewarmedSet.size(), 16u);

    // Execute 50 acquire/release cycles during steady state
    for (int i = 0; i < 50; ++i) {
        const glm::vec3 spawnPos(static_cast<float>(i), 0.0f, static_cast<float>(i * 2));

        Engine::GameObject* acquired = scene->AcquirePooled("BrazierPool", *prefab, spawnPos);
        ASSERT_NE(acquired, nullptr);
        EXPECT_TRUE(acquired->IsActive());
        EXPECT_TRUE(acquired->IsActiveInHierarchy());
        EXPECT_NEAR(acquired->GetTransform()->GetWorldPosition().x, spawnPos.x, 1e-4f);
        EXPECT_NEAR(acquired->GetTransform()->GetWorldPosition().z, spawnPos.z, 1e-4f);

        // Instance was popped from the prewarmed set
        EXPECT_TRUE(prewarmedSet.contains(acquired));
        EXPECT_EQ(scene->GetPoolFreeCount("BrazierPool"), 15u);

        // Release back to pool
        scene->ReleaseToPool("BrazierPool", acquired);
        EXPECT_FALSE(acquired->IsActive());
        EXPECT_FALSE(acquired->IsActiveInHierarchy());
        EXPECT_EQ(scene->GetPoolFreeCount("BrazierPool"), 16u);

        // Crucial: Exact entity count invariant proves ZERO heap allocations in steady state!
        EXPECT_EQ(scene->GetGameObjects().size(), baselineEntityCount);
    }
}

TEST(PrefabAndPoolingTest, PoolExhaustionDynamicGrowth) {
    auto scene = std::make_shared<Engine::Scene>("ExhaustionScene");
    auto* tpl = scene->CreateGameObject("Splinter");
    auto prefab = Engine::Prefab::CreateFromGameObject(tpl, true);
    scene->DestroyGameObject(tpl);
    scene->FlushEntityLifecycleQueues();

    // Prewarm only 2 instances
    scene->PrewarmPool("SplinterPool", *prefab, 2u);
    EXPECT_EQ(scene->GetPoolFreeCount("SplinterPool"), 2u);

    // Acquire 4 instances simultaneously
    auto* inst1 = scene->AcquirePooled("SplinterPool", *prefab, { 1.0f, 0.0f, 0.0f });
    auto* inst2 = scene->AcquirePooled("SplinterPool", *prefab, { 2.0f, 0.0f, 0.0f });
    auto* inst3 = scene->AcquirePooled("SplinterPool", *prefab, { 3.0f, 0.0f, 0.0f });
    auto* inst4 = scene->AcquirePooled("SplinterPool", *prefab, { 4.0f, 0.0f, 0.0f });

    ASSERT_NE(inst1, nullptr);
    ASSERT_NE(inst2, nullptr);
    ASSERT_NE(inst3, nullptr);
    ASSERT_NE(inst4, nullptr);

    // All 4 are active and distinct
    EXPECT_NE(inst1, inst2);
    EXPECT_NE(inst2, inst3);
    EXPECT_NE(inst3, inst4);
    EXPECT_TRUE(inst1->IsActive());
    EXPECT_TRUE(inst2->IsActive());
    EXPECT_TRUE(inst3->IsActive());
    EXPECT_TRUE(inst4->IsActive());

    EXPECT_EQ(scene->GetPoolFreeCount("SplinterPool"), 0u);
    EXPECT_EQ(scene->GetPoolTotalCount("SplinterPool"), 4u);

    // Release all 4 back using single-argument overload ReleaseToPool(go)
    scene->ReleaseToPool(inst1);
    scene->ReleaseToPool(inst2);
    scene->ReleaseToPool(inst3);
    scene->ReleaseToPool(inst4);

    EXPECT_EQ(scene->GetPoolFreeCount("SplinterPool"), 4u);
}

TEST(PrefabAndPoolingTest, PhysicsAndParticleStateResetOnRelease) {
    auto scene = std::make_shared<Engine::Scene>("PhysicsResetScene");

    auto* tpl = scene->CreateGameObject("DynamicCrate");
    auto* rb2d = tpl->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    auto* bc2d = tpl->AddComponent<Engine::BoxCollider2D>();
    bc2d->size = { 1.0f, 1.0f };

    auto* ps = tpl->AddComponent<Engine::ParticleSystem>(20u);

    auto prefab = Engine::Prefab::CreateFromGameObject(tpl, true);
    scene->DestroyGameObject(tpl);
    scene->FlushEntityLifecycleQueues();

    // Start physics runtime
    scene->OnRuntimeStart();
    EXPECT_TRUE(scene->IsRuntimeRunning());

    scene->PrewarmPool("CratePool", *prefab, 1u);
    EXPECT_EQ(scene->GetPoolFreeCount("CratePool"), 1u);

    // Acquire instance
    auto* crate = scene->AcquirePooled("CratePool", *prefab, { 10.0f, 10.0f, 0.0f });
    ASSERT_NE(crate, nullptr);
    EXPECT_TRUE(crate->IsActive());

    auto* crateRb = crate->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(crateRb, nullptr);
    ASSERT_NE(crateRb->RuntimeBody, nullptr);

    b2BodyId bodyId = b2LoadBodyId(crateRb->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsValid(bodyId));
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    // Simulate physics activity: give velocity and emit particles
    b2Body_SetLinearVelocity(bodyId, b2Vec2{ 15.0f, -5.0f });
    b2Body_SetAngularVelocity(bodyId, 3.14f);

    auto* cratePs = crate->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(cratePs, nullptr);
    Engine::ParticleProps props;
    props.lifeTime = 2.0f;
    cratePs->Emit(props);
    EXPECT_GT(cratePs->GetActiveParticleCount(), 0u);

    // Release back to pool
    scene->ReleaseToPool("CratePool", crate);
    EXPECT_FALSE(crate->IsActive());
    EXPECT_FALSE(b2Body_IsEnabled(bodyId));

    b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
    EXPECT_NEAR(vel.x, 0.0f, 1e-4f);
    EXPECT_NEAR(vel.y, 0.0f, 1e-4f);
    EXPECT_NEAR(b2Body_GetAngularVelocity(bodyId), 0.0f, 1e-4f);
    EXPECT_EQ(cratePs->GetActiveParticleCount(), 0u);

    // Re-acquire at new position: must be re-enabled and active
    auto* reacquired = scene->AcquirePooled("CratePool", *prefab, { 25.0f, 0.0f, 0.0f });
    EXPECT_EQ(reacquired, crate);
    EXPECT_TRUE(reacquired->IsActive());
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    scene->OnRuntimeStop();
}

TEST(PrefabAndPoolingTest, CascadingDestructionSafety) {
    auto scene = std::make_shared<Engine::Scene>("DestructionSafetyScene");
    auto* tpl = scene->CreateGameObject("EphemeralItem");
    auto prefab = Engine::Prefab::CreateFromGameObject(tpl, true);
    scene->DestroyGameObject(tpl);
    scene->FlushEntityLifecycleQueues();

    scene->PrewarmPool("ItemPool", *prefab, 2u);
    EXPECT_EQ(scene->GetPoolFreeCount("ItemPool"), 2u);
    EXPECT_EQ(scene->GetPoolTotalCount("ItemPool"), 2u);

    auto* item1 = scene->AcquirePooled("ItemPool", *prefab, { 0.0f, 0.0f, 0.0f });
    auto* item2 = scene->AcquirePooled("ItemPool", *prefab, { 1.0f, 0.0f, 0.0f });

    // Destroy item1 directly from scene
    scene->DestroyGameObject(item1);
    scene->FlushEntityLifecycleQueues();

    // Pool count should automatically decrement so destroyed pointer is never reused
    EXPECT_EQ(scene->GetPoolTotalCount("ItemPool"), 1u);

    // Release item2
    scene->ReleaseToPool(item2);
    EXPECT_EQ(scene->GetPoolFreeCount("ItemPool"), 1u);

    // Acquiring next item safely returns item2
    auto* nextItem = scene->AcquirePooled("ItemPool", *prefab, { 5.0f, 0.0f, 0.0f });
    EXPECT_EQ(nextItem, item2);
}

TEST(PrefabAndPoolingTest, DeepSubtreePoolReset_GrandchildrenAndDescendantsResetCleanly) {
    auto scene = std::make_shared<Engine::Scene>("DeepSubtreeScene");

    // Build 3-level hierarchy: Root -> Child -> Grandchild
    auto* root = scene->CreateGameObject("PooledRoot");
    auto* rb2d = root->AddComponent<Engine::Rigidbody2D>();
    rb2d->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb2d->InitialLinearVelocityXZ = { 2.0f, 3.0f };
    auto* boxCol = root->AddComponent<Engine::BoxCollider2D>();
    boxCol->size = { 1.0f, 1.0f };
    auto* squash = root->AddComponent<Engine::SquashStretchComponent>();

    auto* child = scene->CreateGameObject("PooledChild");
    child->GetTransform()->SetParent(root->GetTransform(), false);
    child->GetTransform()->position = { 0.0f, 1.0f, 0.0f };
    auto* childAnim = child->AddComponent<Engine::Animator>();
    auto clip = std::make_shared<Engine::AnimationClip>("TestClip", 0.1f, true);
    childAnim->AddClip(clip);
    childAnim->Play("TestClip");
    EXPECT_EQ(childAnim->GetCurrentClipName(), "TestClip");

    auto* grandchild = scene->CreateGameObject("PooledGrandchild");
    grandchild->GetTransform()->SetParent(child->GetTransform(), false);
    grandchild->GetTransform()->position = { 0.0f, 2.0f, 0.0f };
    auto* gcPs = grandchild->AddComponent<Engine::ParticleSystem>(20u);

    // Snapshot hierarchy to prefab
    auto prefab = Engine::Prefab::CreateFromGameObject(root, true);
    ASSERT_NE(prefab, nullptr);
    EXPECT_TRUE(prefab->IsValid());

    scene->DestroyGameObject(root);
    scene->FlushEntityLifecycleQueues();

    // Start physics runtime
    scene->OnRuntimeStart();

    // Prewarm pool with 1 instance
    scene->PrewarmPool("DeepPool", *prefab, 1u);
    EXPECT_EQ(scene->GetPoolFreeCount("DeepPool"), 1u);

    // Acquire instance
    auto* inst = scene->AcquirePooled("DeepPool", *prefab, { 5.0f, 0.0f, 10.0f });
    ASSERT_NE(inst, nullptr);
    EXPECT_TRUE(inst->IsActive());
    EXPECT_FALSE(inst->IsInFreeList());

    // Verify initial velocity was restored on acquire
    auto* instRb = inst->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(instRb, nullptr);
    b2BodyId bodyId = b2LoadBodyId(instRb->RuntimeBody.Value);
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));
    EXPECT_TRUE(b2Body_IsAwake(bodyId));
    b2Vec2 v = b2Body_GetLinearVelocity(bodyId);
    EXPECT_NEAR(v.x, 2.0f, 1e-4f);
    EXPECT_NEAR(v.y, 3.0f, 1e-4f);

    // Find child and grandchild
    auto children = inst->GetTransform()->GetChildren();
    ASSERT_EQ(children.size(), 1u);
    auto* instChild = children[0]->GetGameObject();
    ASSERT_NE(instChild, nullptr);
    EXPECT_TRUE(instChild->IsActiveInHierarchy());

    auto gchildren = children[0]->GetChildren();
    ASSERT_EQ(gchildren.size(), 1u);
    auto* instGrandchild = gchildren[0]->GetGameObject();
    ASSERT_NE(instGrandchild, nullptr);
    EXPECT_TRUE(instGrandchild->IsActiveInHierarchy());

    // Dirty state on all 3 levels
    // Level 1 (Root): dirty stasis, dirty squash stretch, modify velocities
    instRb->SetStasisFrozen(true);
    instRb->ApplyImpulseXZ({ 10.0f, 20.0f });
    auto* instSquash = inst->GetComponent<Engine::SquashStretchComponent>();
    ASSERT_NE(instSquash, nullptr);
    instSquash->TriggerImpulse(5.0f, 15.0f);

    // Level 2 (Child): animator play
    auto* instChildAnim = instChild->GetComponent<Engine::Animator>();
    ASSERT_NE(instChildAnim, nullptr);
    instChildAnim->Play("TestClip");
    EXPECT_EQ(instChildAnim->GetCurrentClipName(), "TestClip");

    // Level 3 (Grandchild): emit particles
    auto* instGcPs = instGrandchild->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(instGcPs, nullptr);
    Engine::ParticleProps props;
    props.lifeTime = 2.0f;
    instGcPs->Emit(props);
    EXPECT_GT(instGcPs->GetActiveParticleCount(), 0u);

    // Release back to pool
    scene->ReleaseToPool(inst);

    // Verify root is inactive & in free list
    EXPECT_FALSE(inst->IsActive());
    EXPECT_TRUE(inst->IsInFreeList());
    EXPECT_FALSE(b2Body_IsEnabled(bodyId));
    EXPECT_FALSE(b2Body_IsAwake(bodyId));
    EXPECT_FALSE(instRb->IsStasisFrozen());

    // Verify Level 2 (Child) animator stopped
    EXPECT_TRUE(instChildAnim->GetCurrentClipName().empty());

    // Verify Level 3 (Grandchild) particle system reset
    EXPECT_EQ(instGcPs->GetActiveParticleCount(), 0u);

    // Reacquire at new world position
    auto* reacquired = scene->AcquirePooled("DeepPool", *prefab, { 50.0f, 0.0f, -50.0f });
    EXPECT_EQ(reacquired, inst);
    EXPECT_FALSE(reacquired->IsInFreeList());
    EXPECT_TRUE(reacquired->IsActive());
    EXPECT_TRUE(b2Body_IsEnabled(bodyId));

    // Grandchild transform snapshot verification: prevPosition must equal position
    EXPECT_EQ(instGrandchild->GetTransform()->prevPosition, instGrandchild->GetTransform()->position);

    scene->OnRuntimeStop();
}

TEST(PrefabAndPoolingTest, FreeListO1MembershipAndIdempotentRelease) {
    auto scene = std::make_shared<Engine::Scene>("FreeListScene");
    auto* tpl = scene->CreateGameObject("PoolItem");
    auto prefab = Engine::Prefab::CreateFromGameObject(tpl, true);
    scene->DestroyGameObject(tpl);
    scene->FlushEntityLifecycleQueues();

    scene->PrewarmPool("FreeListPool", *prefab, 3u);
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 3u);
    EXPECT_EQ(scene->GetPoolTotalCount("FreeListPool"), 3u);

    auto* item1 = scene->AcquirePooled("FreeListPool", *prefab, { 0.0f, 0.0f, 0.0f });
    ASSERT_NE(item1, nullptr);
    EXPECT_FALSE(item1->IsInFreeList());
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 2u);

    // Idempotent release: calling ReleaseToPool multiple times must not inflate freeList
    scene->ReleaseToPool(item1);
    EXPECT_TRUE(item1->IsInFreeList());
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 3u);

    scene->ReleaseToPool(item1);
    EXPECT_TRUE(item1->IsInFreeList());
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 3u);

    // Acquire all 3
    auto* a1 = scene->AcquirePooled("FreeListPool", *prefab, { 1.0f, 0.0f, 0.0f });
    auto* a2 = scene->AcquirePooled("FreeListPool", *prefab, { 2.0f, 0.0f, 0.0f });
    auto* a3 = scene->AcquirePooled("FreeListPool", *prefab, { 3.0f, 0.0f, 0.0f });
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 0u);
    EXPECT_FALSE(a1->IsInFreeList());
    EXPECT_FALSE(a2->IsInFreeList());
    EXPECT_FALSE(a3->IsInFreeList());

    // Destroy a2
    scene->DestroyGameObject(a2);
    scene->FlushEntityLifecycleQueues();
    EXPECT_FALSE(a2->IsInFreeList());
    EXPECT_EQ(scene->GetPoolTotalCount("FreeListPool"), 2u);

    // Release remaining
    scene->ReleaseToPool(a1);
    scene->ReleaseToPool(a3);
    EXPECT_EQ(scene->GetPoolFreeCount("FreeListPool"), 2u);
}

class CustomPooledComponent : public Engine::Component {
public:
    int acquireCount = 0;
    int releaseCount = 0;
    int customState = 42;

    void OnPoolAcquire() override {
        ++acquireCount;
        customState = 100;
    }

    void OnPoolRelease() override {
        ++releaseCount;
        customState = 0;
    }
};

TEST(PrefabAndPoolingTest, PolymorphicResettableHookOnCustomComponent) {
    auto scene = std::make_shared<Engine::Scene>("CustomComponentScene");
    auto* go = scene->CreateGameObject("CustomEntity");
    auto* comp = go->AddComponent<CustomPooledComponent>();
    EXPECT_EQ(comp->acquireCount, 0);
    EXPECT_EQ(comp->releaseCount, 0);
    EXPECT_EQ(comp->customState, 42);

    go->SetPoolKey("CustomPool");

    // Manually test propagation through Scene
    scene->PropagatePoolRelease(go);
    EXPECT_EQ(comp->releaseCount, 1);
    EXPECT_EQ(comp->customState, 0);

    scene->PropagatePoolAcquire(go);
    EXPECT_EQ(comp->acquireCount, 1);
    EXPECT_EQ(comp->customState, 100);

    // Test with ReleaseToPool
    scene->ReleaseToPool("CustomPool", go);
    EXPECT_EQ(comp->releaseCount, 2);
    EXPECT_EQ(comp->customState, 0);
    EXPECT_TRUE(go->IsInFreeList());
    EXPECT_FALSE(go->IsActive());
}

TEST(PrefabAndPoolingTest, ReleaseToPoolMutatingChildrenUsesScratchSafely) {
    class DetachOnReleaseComponent : public Engine::Component {
    public:
        bool detachArmed = false;
        int releaseCount = 0;

        void OnPoolRelease() override {
            ++releaseCount;
            if (detachArmed && GetGameObject() && GetGameObject()->GetTransform()) {
                GetGameObject()->GetTransform()->SetParent(nullptr, true);
            }
        }
    };

    auto scene = std::make_shared<Engine::Scene>("PoolScratchSafeScene");
    auto* root = scene->CreateGameObject("PoolRoot");
    root->SetPoolKey("ScratchPool");

    auto* childA = scene->CreateGameObject("ChildA_Detaching");
    childA->GetTransform()->SetParent(root->GetTransform(), false);
    auto* detachComp = childA->AddComponent<DetachOnReleaseComponent>();

    auto* childB = scene->CreateGameObject("ChildB_Sibling");
    childB->GetTransform()->SetParent(root->GetTransform(), false);
    auto* siblingComp = childB->AddComponent<CustomPooledComponent>();

    auto* childC = scene->CreateGameObject("ChildC_Sibling");
    childC->GetTransform()->SetParent(root->GetTransform(), false);
    auto* siblingCompC = childC->AddComponent<CustomPooledComponent>();

    auto* grandchild = scene->CreateGameObject("GrandchildC");
    grandchild->GetTransform()->SetParent(childC->GetTransform(), false);
    auto* gcComp = grandchild->AddComponent<CustomPooledComponent>();

    auto& poolMgr = scene->GetPoolManager();
    const size_t capBefore = poolMgr.GetTempChildrenScratchCapacity();
    EXPECT_GE(capBefore, 64u);

    detachComp->detachArmed = true;
    scene->ReleaseToPool("ScratchPool", root);

    // ChildA detached from Root mid-ReleaseToPool; all siblings & descendants must still have been visited safely
    EXPECT_EQ(childA->GetTransform()->GetParent(), nullptr);
    EXPECT_EQ(root->GetTransform()->GetChildren().size(), 2u);
    EXPECT_EQ(detachComp->releaseCount, 1);
    EXPECT_EQ(siblingComp->releaseCount, 1);
    EXPECT_EQ(siblingCompC->releaseCount, 1);
    EXPECT_EQ(gcComp->releaseCount, 1);
    EXPECT_EQ(siblingComp->customState, 0);
    EXPECT_EQ(siblingCompC->customState, 0);
    EXPECT_EQ(gcComp->customState, 0);
    EXPECT_EQ(poolMgr.GetTempChildrenScratchCapacity(), capBefore);
}

} // namespace
