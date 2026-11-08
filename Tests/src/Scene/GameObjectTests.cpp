#include <gtest/gtest.h>
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Component.h"
#include "Engine/Physics/Collision2DInfo.h"
#include <memory>

namespace {

class LifecycleComponent : public Engine::Component {
public:
    bool* destroyedFlag = nullptr;
    bool awakeCalled = false;

    explicit LifecycleComponent(bool* onDestroyOut = nullptr)
        : destroyedFlag(onDestroyOut) {}

    void Awake() override {
        awakeCalled = true;
    }

    void OnDestroy() override {
        if (destroyedFlag) {
            *destroyedFlag = true;
        }
    }
};

TEST(GameObjectTest, CachedTransformMatchesGetComponentAndCannotBeRemoved) {
    Engine::GameObject entity("Hero");

    ASSERT_NE(entity.GetTransform(), nullptr);
    EXPECT_EQ(entity.GetTransform(), entity.GetComponent<Engine::Transform>());
    EXPECT_EQ(entity.GetName(), "Hero");

    entity.SetName("RenamedHero");
    EXPECT_EQ(entity.GetName(), "RenamedHero");

    // Mandatory Transform cannot be removed
    EXPECT_FALSE(entity.RemoveComponent<Engine::Transform>());
    EXPECT_NE(entity.GetTransform(), nullptr);
}

TEST(GameObjectTest, UUIDGenerationAndExplicitUUIDPreservation) {
    Engine::GameObject a("EntityA");
    Engine::GameObject b("EntityB");
    EXPECT_NE(a.GetUUID(), 0u);
    EXPECT_NE(b.GetUUID(), 0u);
    EXPECT_NE(a.GetUUID(), b.GetUUID());

    constexpr uint64_t kExplicitUUID = 987654321012345678ULL;
    Engine::GameObject explicitEntity(kExplicitUUID, "RestoredEntity");
    EXPECT_EQ(explicitEntity.GetUUID(), kExplicitUUID);
}

TEST(GameObjectTest, ComponentLifecycleAddAndRemove) {
    Engine::GameObject entity("TestObject");
    bool wasDestroyed = false;

    auto* comp = entity.AddComponent<LifecycleComponent>(&wasDestroyed);
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->awakeCalled);
    EXPECT_FALSE(wasDestroyed);
    EXPECT_EQ(entity.GetComponent<LifecycleComponent>(), comp);

    EXPECT_TRUE(entity.RemoveComponent<LifecycleComponent>());
    EXPECT_TRUE(wasDestroyed);
    EXPECT_EQ(entity.GetComponent<LifecycleComponent>(), nullptr);
    EXPECT_FALSE(entity.RemoveComponent<LifecycleComponent>());
}

TEST(GameObjectTest, SceneDestroyGameObjectRemovesEntity) {
    Engine::Scene scene("DeletionTestScene");
    auto* a = scene.CreateGameObject("First");
    auto* b = scene.CreateGameObject("Second");
    ASSERT_EQ(scene.GetGameObjects().size(), 2u);

    scene.DestroyGameObject(a);
    ASSERT_EQ(scene.GetGameObjects().size(), 1u);
    EXPECT_EQ(scene.GetGameObjects()[0].get(), b);
}

TEST(TransformTest, TranslationRotationAndMatrix) {
    Engine::Transform t;
    EXPECT_EQ(t.position, glm::vec3(0.0f));
    EXPECT_EQ(t.rotation, glm::vec3(0.0f));
    EXPECT_EQ(t.scale, glm::vec3(1.0f));

    t.Translate({ 1.0f, 2.0f, 3.0f });
    EXPECT_EQ(t.position, glm::vec3(1.0f, 2.0f, 3.0f));

    t.Rotate({ 90.0f, 0.0f, 0.0f });
    EXPECT_EQ(t.rotation, glm::vec3(90.0f, 0.0f, 0.0f));

    t.scale = { 2.0f, 2.0f, 2.0f };
    glm::mat4 mat = t.GetTransformMatrix();
    // Translation column in column-major matrix
    EXPECT_FLOAT_EQ(mat[3][0], 1.0f);
    EXPECT_FLOAT_EQ(mat[3][1], 2.0f);
    EXPECT_FLOAT_EQ(mat[3][2], 3.0f);
}

class DynamicComponentB : public Engine::Component {
public:
    int* pStartCount = nullptr;
    int* pUpdateCount = nullptr;
    int* pCollisionCount = nullptr;

    void Start() override {
        if (pStartCount) ++(*pStartCount);
    }

    void Update(float deltaTime) override {
        (void)deltaTime;
        if (pUpdateCount) ++(*pUpdateCount);
    }

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        (void)info;
        if (pCollisionCount) ++(*pCollisionCount);
    }
};

class MutatingComponentA : public Engine::Component {
public:
    int* pUpdateCount = nullptr;
    int* pDestroyCount = nullptr;
    int* pCollisionCount = nullptr;
    int* pBStartCount = nullptr;
    int* pBUpdateCount = nullptr;
    int* pBCollisionCount = nullptr;
    bool mutateOnUpdate = false;
    bool mutateOnCollision = false;

    void Update(float deltaTime) override {
        (void)deltaTime;
        if (pUpdateCount) ++(*pUpdateCount);

        if (mutateOnUpdate && GetGameObject()) {
            auto* b = GetGameObject()->AddComponent<DynamicComponentB>();
            b->pStartCount = pBStartCount;
            b->pUpdateCount = pBUpdateCount;
            b->pCollisionCount = pBCollisionCount;
            GetGameObject()->RemoveComponent<MutatingComponentA>();
        }
    }

    void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
        (void)info;
        if (pCollisionCount) ++(*pCollisionCount);

        if (mutateOnCollision && GetGameObject()) {
            auto* b = GetGameObject()->AddComponent<DynamicComponentB>();
            b->pStartCount = pBStartCount;
            b->pUpdateCount = pBUpdateCount;
            b->pCollisionCount = pBCollisionCount;
            GetGameObject()->RemoveComponent<MutatingComponentA>();
        }
    }

    void OnDestroy() override {
        if (pDestroyCount) ++(*pDestroyCount);
    }
};

TEST(GameObjectTest, ComponentAddAndSelfRemoveDuringUpdate) {
    Engine::Scene scene("ComponentUpdateMutationScene");
    auto* entity = scene.CreateGameObject("Hero");

    int aUpdateCount = 0;
    int aDestroyCount = 0;
    int bStartCount = 0;
    int bUpdateCount = 0;

    auto* compA = entity->AddComponent<MutatingComponentA>();
    compA->pUpdateCount = &aUpdateCount;
    compA->pDestroyCount = &aDestroyCount;
    compA->pBStartCount = &bStartCount;
    compA->pBUpdateCount = &bUpdateCount;
    compA->mutateOnUpdate = true;

    EXPECT_TRUE(entity->HasComponent<MutatingComponentA>());
    EXPECT_FALSE(entity->HasComponent<DynamicComponentB>());

    // Frame 1 update: CompA updates, spawns CompB, removes CompA
    scene.Update(0.016f);

    EXPECT_EQ(aUpdateCount, 1);
    EXPECT_EQ(aDestroyCount, 1);
    EXPECT_EQ(bStartCount, 1);
    EXPECT_EQ(bUpdateCount, 1);

    EXPECT_FALSE(entity->HasComponent<MutatingComponentA>());
    EXPECT_EQ(entity->GetComponent<MutatingComponentA>(), nullptr);
    EXPECT_TRUE(entity->HasComponent<DynamicComponentB>());
    EXPECT_NE(entity->GetComponent<DynamicComponentB>(), nullptr);

    // Frame 2 update: Only CompB updates
    scene.Update(0.016f);

    EXPECT_EQ(aUpdateCount, 1);
    EXPECT_EQ(bStartCount, 1);
    EXPECT_EQ(bUpdateCount, 2);
}

TEST(GameObjectTest, ComponentAddAndSelfRemoveDuringCollision) {
    Engine::GameObject entity("CombatEntity");

    int aCollisionCount = 0;
    int aDestroyCount = 0;
    int bCollisionCount = 0;

    auto* compA = entity.AddComponent<MutatingComponentA>();
    compA->pCollisionCount = &aCollisionCount;
    compA->pDestroyCount = &aDestroyCount;
    compA->pBCollisionCount = &bCollisionCount;
    compA->mutateOnCollision = true;

    Engine::Collision2DInfo info;
    info.ApproachSpeed = 10.0f;

    EXPECT_TRUE(entity.HasComponent<MutatingComponentA>());
    EXPECT_FALSE(entity.HasComponent<DynamicComponentB>());

    // Dispatch collision: A runs, adds B, removes A. B runs within same dispatch cleanly!
    entity.DispatchCollisionEnter2D(info);

    EXPECT_EQ(aCollisionCount, 1);
    EXPECT_EQ(aDestroyCount, 1);
    EXPECT_EQ(bCollisionCount, 1);

    EXPECT_FALSE(entity.HasComponent<MutatingComponentA>());
    EXPECT_EQ(entity.GetComponent<MutatingComponentA>(), nullptr);
    EXPECT_TRUE(entity.HasComponent<DynamicComponentB>());
    EXPECT_NE(entity.GetComponent<DynamicComponentB>(), nullptr);
}

TEST(GameObjectTest, MidDispatchDeactivationHaltsSubsequentComponents) {
    Engine::GameObject entity("DeactivatingEntity");

    class DisablingComponent : public Engine::Component {
    public:
        int callCount = 0;
        void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
            (void)info;
            ++callCount;
            if (GetGameObject()) {
                GetGameObject()->SetActive(false);
            }
        }
    };

    class TrailingComponent : public Engine::Component {
    public:
        int callCount = 0;
        void OnCollisionEnter2D(const Engine::Collision2DInfo& info) override {
            (void)info;
            ++callCount;
        }
    };

    auto* disabler = entity.AddComponent<DisablingComponent>();
    auto* trailer = entity.AddComponent<TrailingComponent>();

    Engine::Collision2DInfo info;
    entity.DispatchCollisionEnter2D(info);

    EXPECT_EQ(disabler->callCount, 1);
    EXPECT_EQ(trailer->callCount, 0); // Trailing component must NOT receive callback!
    EXPECT_FALSE(entity.IsActiveInHierarchy());
}

} // namespace

