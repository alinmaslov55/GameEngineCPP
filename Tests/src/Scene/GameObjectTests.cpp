#include <gtest/gtest.h>
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Component.h"
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

} // namespace

