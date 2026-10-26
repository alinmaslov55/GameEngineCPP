#include <gtest/gtest.h>

#include "Engine/Core/ResourceManager.h"
#include "Engine/Math/Isometric.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Utils/Logger.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace {

struct DummyAsset {
    std::string loadedPath;
    static inline int constructCount = 0;

    explicit DummyAsset(const std::string& path) : loadedPath(path) {
        ++constructCount;
    }
};

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

} // namespace

// ==========================================
// 1. Isometric Projection & Unprojection Tests
// ==========================================
TEST(IsometricMathTest, OriginMapsToOrigin) {
    const glm::vec2 screen = Engine::Isometric::WorldToScreen({0.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(screen.x, 0.0f);
    EXPECT_FLOAT_EQ(screen.y, 0.0f);

    const glm::vec3 world = Engine::Isometric::ScreenToWorld(screen);
    EXPECT_FLOAT_EQ(world.x, 0.0f);
    EXPECT_FLOAT_EQ(world.y, 0.0f);
    EXPECT_FLOAT_EQ(world.z, 0.0f);
}

TEST(IsometricMathTest, GroundPlaneRoundTripPreservesCoordinates) {
    const glm::vec3 samples[] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {4.25f, 0.0f, 7.75f},
        {-3.5f, 0.0f, 2.25f},
        {-6.125f, 0.0f, -9.875f}
    };

    for (const auto& original : samples) {
        const glm::vec2 screen = Engine::Isometric::WorldToScreen(original);
        const glm::vec3 roundTripped = Engine::Isometric::ScreenToWorld(screen);
        EXPECT_NEAR(roundTripped.x, original.x, 1e-5f);
        EXPECT_FLOAT_EQ(roundTripped.y, 0.0f);
        EXPECT_NEAR(roundTripped.z, original.z, 1e-5f);
    }
}

TEST(IsometricMathTest, VerticalElevationOffsetsScreenY) {
    const glm::vec2 ground = Engine::Isometric::WorldToScreen({2.0f, 0.0f, 3.0f});
    const glm::vec2 elevated = Engine::Isometric::WorldToScreen({2.0f, 1.75f, 3.0f});

    EXPECT_FLOAT_EQ(elevated.x, ground.x);
    EXPECT_NEAR(elevated.y - ground.y, 1.75f, 1e-5f);
}

// ==========================================
// 2. GameObject, Cached Transform, UUID & Component Removal Tests
// ==========================================
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

// ==========================================
// 3. EditorCamera Zoom, Pan & Reset Tests
// ==========================================
TEST(EditorCameraTest, ScrollPanAndResetUpdateStateDeterministically) {
    Engine::EditorCamera editorCam(16.0f / 9.0f, 4.0f);
    const glm::vec3 defaultPos = editorCam.GetPosition();
    EXPECT_FLOAT_EQ(editorCam.GetZoomLevel(), 4.0f);

    // Scrolling up zooms in (smaller orthographic zoom level)
    editorCam.OnMouseScroll(2.0f);
    EXPECT_LT(editorCam.GetZoomLevel(), 4.0f);

    // Panning shifts camera position away from initial default
    editorCam.Pan({128.0f, -72.0f}, {1280.0f, 720.0f});
    EXPECT_NE(editorCam.GetPosition().x, defaultPos.x);
    EXPECT_NE(editorCam.GetPosition().y, defaultPos.y);

    // Reset restores canonical position and zoom
    editorCam.Reset();
    EXPECT_FLOAT_EQ(editorCam.GetZoomLevel(), 3.5f);
    EXPECT_FLOAT_EQ(editorCam.GetPosition().x, 0.0f);
    EXPECT_FLOAT_EQ(editorCam.GetPosition().y, 2.25f);
}

// ==========================================
// 4. Isometric Physics: Gravity & Tile Walkability Tests
// ==========================================
TEST(IsometricPhysicsTest, JumpArcRisesAndReturnsToGroundLevel) {
    Engine::Scene scene("GravityScene");
    auto* player = scene.CreateGameObject("Player");
    auto* transform = player->GetTransform();
    transform->position = {2.0f, 0.0f, 2.0f};

    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->useGravity = true;
    collider->gravity = -10.0f;
    collider->groundLevel = 0.0f;

    EXPECT_TRUE(collider->IsGrounded(transform->position.y));

    // Apply upward jump impulse
    collider->verticalVelocity = 5.0f;
    scene.Update(0.1f);
    EXPECT_GT(transform->position.y, 0.0f);
    EXPECT_FALSE(collider->IsGrounded(transform->position.y));

    // Simulate enough frames for the jump arc to complete and clamp to groundLevel
    for (int step = 0; step < 30; ++step) {
        scene.Update(0.05f);
    }

    EXPECT_FLOAT_EQ(transform->position.y, 0.0f);
    EXPECT_FLOAT_EQ(collider->verticalVelocity, 0.0f);
    EXPECT_TRUE(collider->IsGrounded(transform->position.y));
}

TEST(IsometricPhysicsTest, NonWalkableTileBlocksDynamicColliderWhileWalkableAllowsPassage) {
    Engine::Scene scene("WalkabilityScene");

    auto* mapObj = scene.CreateGameObject("LevelGrid");
    auto* tilemap = mapObj->AddComponent<Engine::Tilemap>(5, 5);
    for (uint32_t z = 0; z < 5; ++z) {
        for (uint32_t x = 0; x < 5; ++x) {
            tilemap->SetTile(x, z, 1, true); // Walkable grass
        }
    }
    // Place an impassable wall tile at (2, 2), spanning X=[1.5..2.5], Z=[1.5..2.5]
    tilemap->SetTile(2, 2, 2, false);

    auto* player = scene.CreateGameObject("Player");
    auto* playerTransform = player->GetTransform();
    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->size = {0.6f, 0.6f};
    collider->useGravity = false;

    // 1. Walkable tile at (1, 2): player stays unimpeded
    playerTransform->position = {1.0f, 0.0f, 2.0f};
    scene.Update(0.016f);
    EXPECT_NEAR(playerTransform->position.x, 1.0f, 1e-5f);
    EXPECT_NEAR(playerTransform->position.z, 2.0f, 1e-5f);

    // 2. Move into non-walkable wall at (2, 2) from the left (X = 1.4 -> right edge 1.7 overlaps wall left edge 1.5)
    playerTransform->position = {1.4f, 0.0f, 2.0f};
    scene.Update(0.016f);

    // Player right bound (pos.x + 0.3) must be pushed out to <= wall left bound (1.5)
    EXPECT_NEAR(playerTransform->position.x, 1.2f, 1e-4f);
    EXPECT_NEAR(playerTransform->position.z, 2.0f, 1e-4f);
}

// ==========================================
// 4b. Box2D 2D RigidBody & Collision Physics Tests
// ==========================================
TEST(Box2DPhysicsTest, DynamicBodyFallsUnderGravityDuringRuntime) {
    Engine::Scene scene("Box2DScene");

    auto* ground = scene.CreateGameObject("Ground");
    ground->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* groundRb = ground->AddComponent<Engine::Rigidbody2D>();
    groundRb->Type = Engine::Rigidbody2D::BodyType::Static;
    auto* groundBox = ground->AddComponent<Engine::BoxCollider2D>();
    groundBox->size = { 10.0f, 1.0f };

    auto* ball = scene.CreateGameObject("Ball");
    ball->GetTransform()->position = { 0.0f, 10.0f, 0.0f };
    auto* ballRb = ball->AddComponent<Engine::Rigidbody2D>();
    ballRb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    ballRb->FixedRotation = true;
    auto* ballBox = ball->AddComponent<Engine::BoxCollider2D>();
    ballBox->size = { 1.0f, 1.0f };
    ballBox->density = 1.0f;
    ballBox->restitution = 0.5f;

    // Prior to OnRuntimeStart, Box2D body handles are nullptr
    EXPECT_EQ(ballRb->RuntimeBody, nullptr);
    EXPECT_EQ(groundRb->RuntimeBody, nullptr);

    scene.OnRuntimeStart();
    EXPECT_NE(ballRb->RuntimeBody, nullptr);
    EXPECT_NE(groundRb->RuntimeBody, nullptr);
    EXPECT_NE(ballBox->RuntimeFixture, nullptr);

    const float initialY = ball->GetTransform()->position.y;
    EXPECT_FLOAT_EQ(initialY, 10.0f);

    // Step the physics simulation
    for (int step = 0; step < 20; ++step) {
        scene.UpdateRuntime(0.016f);
    }

    // Ball should have accelerated and fallen downward
    EXPECT_LT(ball->GetTransform()->position.y, initialY);

    scene.OnRuntimeStop();
    EXPECT_EQ(ballRb->RuntimeBody, nullptr);
    EXPECT_EQ(groundRb->RuntimeBody, nullptr);
    EXPECT_EQ(ballBox->RuntimeFixture, nullptr);
}

// ==========================================
// 5. SceneSerializer In-Memory & File Round-Trip Tests
// ==========================================
TEST(SceneSerializerTest, SerializeAndDeserializeRoundTripPreservesSceneAndComponents) {
    auto sourceScene = std::make_shared<Engine::Scene>("TestSerializedLevel");
    sourceScene->SetOrthographicSize(2.75f);

    // Entity 1: Tilemap with mixed walkability
    constexpr uint64_t kMapUUID = 111111111111ULL;
    auto* mapEntity = sourceScene->CreateGameObjectWithUUID(kMapUUID, "WorldMap");
    auto* map = mapEntity->AddComponent<Engine::Tilemap>(4, 3);
    map->SetTile(0, 0, 1, true);
    map->SetTile(1, 2, 2, false);
    map->SetTile(3, 1, 3, false);

    // Entity 2: Dynamic actor with Transform, BoxCollider2D, ParticleSystem, CameraComponent
    constexpr uint64_t kHeroUUID = 222222222222ULL;
    auto* hero = sourceScene->CreateGameObjectWithUUID(kHeroUUID, "Hero");
    hero->GetTransform()->position = {3.5f, 1.25f, -2.0f};
    hero->GetTransform()->rotation = {0.0f, 45.0f, 15.0f};
    hero->GetTransform()->scale = {1.5f, 2.0f, 1.25f};

    auto* rb = hero->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    rb->FixedRotation = true;

    auto* box = hero->AddComponent<Engine::BoxCollider2D>();
    box->size = {0.8f, 0.9f};
    box->offset = {0.1f, -0.2f};
    box->density = 2.5f;
    box->friction = 0.4f;
    box->restitution = 0.75f;
    box->isDynamic = true;
    box->useGravity = true;
    box->gravity = -14.5f;
    box->groundLevel = 0.25f;

    hero->AddComponent<Engine::ParticleSystem>(256);

    auto* cam = hero->AddComponent<Engine::CameraComponent>();
    cam->OrthographicSize = 3.2f;
    cam->FixedAspectRatio = true;

    // Entity 3: Multiple UIPanels on a single HUD GameObject
    auto* hud = sourceScene->CreateGameObject("HUD");
    hud->AddComponent<Engine::UIPanel>(glm::vec2{20.0f, 40.0f}, glm::vec2{200.0f, 30.0f}, glm::vec4{0.2f, 0.3f, 0.4f, 0.9f});
    hud->AddComponent<Engine::UIPanel>(glm::vec2{24.0f, 44.0f}, glm::vec2{120.0f, 22.0f}, glm::vec4{0.1f, 0.9f, 0.2f, 1.0f});

    // 1. Test In-Memory Round Trip (used by Play / Stop mode)
    Engine::SceneSerializer sourceSerializer(sourceScene);
    const std::string jsonPayload = sourceSerializer.SerializeToString();
    ASSERT_FALSE(jsonPayload.empty());

    auto clonedScene = std::make_shared<Engine::Scene>("Placeholder");
    Engine::SceneSerializer cloneSerializer(clonedScene);
    ASSERT_TRUE(cloneSerializer.DeserializeFromString(jsonPayload));

    EXPECT_EQ(clonedScene->GetName(), "TestSerializedLevel");
    EXPECT_NEAR(clonedScene->GetOrthographicSize(), 2.75f, 1e-5f);
    ASSERT_EQ(clonedScene->GetGameObjects().size(), 3u);

    auto* clonedMapObj = clonedScene->GetGameObjects()[0].get();
    EXPECT_EQ(clonedMapObj->GetUUID(), kMapUUID);
    auto* clonedMap = clonedMapObj->GetComponent<Engine::Tilemap>();
    ASSERT_NE(clonedMap, nullptr);
    EXPECT_EQ(clonedMap->GetWidth(), 4u);
    EXPECT_EQ(clonedMap->GetHeight(), 3u);
    EXPECT_EQ(clonedMap->GetTile(0, 0).id, 1u);
    EXPECT_TRUE(clonedMap->GetTile(0, 0).walkable);
    EXPECT_EQ(clonedMap->GetTile(1, 2).id, 2u);
    EXPECT_FALSE(clonedMap->GetTile(1, 2).walkable);
    EXPECT_EQ(clonedMap->GetTile(3, 1).id, 3u);
    EXPECT_FALSE(clonedMap->GetTile(3, 1).walkable);

    auto* clonedHero = clonedScene->GetGameObjects()[1].get();
    EXPECT_EQ(clonedHero->GetUUID(), kHeroUUID);
    EXPECT_EQ(clonedHero->GetName(), "Hero");
    EXPECT_NEAR(clonedHero->GetTransform()->position.x, 3.5f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->position.y, 1.25f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->position.z, -2.0f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->rotation.y, 45.0f, 1e-5f);
    EXPECT_NEAR(clonedHero->GetTransform()->scale.y, 2.0f, 1e-5f);

    auto* clonedRb = clonedHero->GetComponent<Engine::Rigidbody2D>();
    ASSERT_NE(clonedRb, nullptr);
    EXPECT_EQ(clonedRb->Type, Engine::Rigidbody2D::BodyType::Dynamic);
    EXPECT_TRUE(clonedRb->FixedRotation);

    auto* clonedBox = clonedHero->GetComponent<Engine::BoxCollider2D>();
    ASSERT_NE(clonedBox, nullptr);
    EXPECT_NEAR(clonedBox->density, 2.5f, 1e-5f);
    EXPECT_NEAR(clonedBox->friction, 0.4f, 1e-5f);
    EXPECT_NEAR(clonedBox->restitution, 0.75f, 1e-5f);
    EXPECT_TRUE(clonedBox->isDynamic);
    EXPECT_TRUE(clonedBox->useGravity);
    EXPECT_NEAR(clonedBox->gravity, -14.5f, 1e-5f);
    EXPECT_NEAR(clonedBox->groundLevel, 0.25f, 1e-5f);
    EXPECT_NEAR(clonedBox->size.x, 0.8f, 1e-5f);
    EXPECT_NEAR(clonedBox->offset.y, -0.2f, 1e-5f);

    auto* clonedParticles = clonedHero->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(clonedParticles, nullptr);
    EXPECT_EQ(clonedParticles->GetParticles().size(), 256u);

    auto* clonedCam = clonedHero->GetComponent<Engine::CameraComponent>();
    ASSERT_NE(clonedCam, nullptr);
    EXPECT_NEAR(clonedCam->OrthographicSize, 3.2f, 1e-5f);
    EXPECT_TRUE(clonedCam->FixedAspectRatio);

    auto* clonedHud = clonedScene->GetGameObjects()[2].get();
    const auto panels = clonedHud->GetComponents<Engine::UIPanel>();
    ASSERT_EQ(panels.size(), 2u);
    EXPECT_NEAR(panels[0]->size.x, 200.0f, 1e-5f);
    EXPECT_NEAR(panels[1]->size.x, 120.0f, 1e-5f);

    // 2. Test Disk File Round Trip
    const std::filesystem::path tempSceneFile = std::filesystem::temp_directory_path() / "engine_unit_test_roundtrip.scene";
    ASSERT_TRUE(sourceSerializer.Serialize(tempSceneFile.string()));

    auto diskScene = std::make_shared<Engine::Scene>();
    Engine::SceneSerializer diskSerializer(diskScene);
    ASSERT_TRUE(diskSerializer.Deserialize(tempSceneFile.string()));
    EXPECT_EQ(diskScene->GetName(), "TestSerializedLevel");
    EXPECT_EQ(diskScene->GetGameObjects().size(), 3u);

    std::error_code ec;
    std::filesystem::remove(tempSceneFile, ec);
}

// ==========================================
// 6. ResourceManager Path Resolution & Caching Tests
// ==========================================
TEST(ResourceManagerTest, CachesAssetsAcrossRelativeAndResolvedPaths) {
    Engine::ResourceManager::ClearType<DummyAsset>();
    DummyAsset::constructCount = 0;

    const std::filesystem::path prevRoot = Engine::ResourceManager::GetAssetRoot();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "engine_test_assets";
    std::filesystem::create_directories(tempRoot / "textures");
    Engine::ResourceManager::SetAssetRoot(tempRoot);

    const std::string relativeKey = "textures/hero.png";
    const std::string resolvedFull = Engine::ResourceManager::ResolvePath(relativeKey);
    EXPECT_EQ(Engine::ResourceManager::ToRelativePath(resolvedFull), relativeKey);

    // First load constructs the asset once
    auto first = Engine::ResourceManager::Load<DummyAsset>(relativeKey);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(DummyAsset::constructCount, 1);

    // Second load via full path resolves to the same normalized relative cache key
    auto second = Engine::ResourceManager::Load<DummyAsset>(resolvedFull);
    EXPECT_EQ(first, second);
    EXPECT_EQ(DummyAsset::constructCount, 1);

    // Get retrieves the cached instance without constructing
    EXPECT_EQ(Engine::ResourceManager::Get<DummyAsset>(relativeKey), first);

    // ClearType evicts all cached instances of DummyAsset
    Engine::ResourceManager::ClearType<DummyAsset>();
    EXPECT_EQ(Engine::ResourceManager::Get<DummyAsset>(relativeKey), nullptr);

    // Restore previous asset root & clean up temp folder
    if (!prevRoot.empty()) {
        Engine::ResourceManager::SetAssetRoot(prevRoot);
    }
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
}

