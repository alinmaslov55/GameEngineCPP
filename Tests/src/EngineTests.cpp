#include <gtest/gtest.h>

#include "Engine/Core/Layer.h"
#include "Engine/Core/ResourceManager.h"
#ifdef ENGINE_BUILD_EDITOR
#include "Engine/ImGui/ImGuiLayer.h"
#endif
#include "Engine/Math/Isometric.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/SpriteRenderer.h"
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

#ifdef ENGINE_BUILD_EDITOR
#include <imgui.h>
#include <imgui_internal.h>
#endif

#if defined(ENGINE_BUILD_EDITOR) == defined(ENGINE_DIST)
#error "Exactly one of ENGINE_BUILD_EDITOR or ENGINE_DIST must be defined."
#endif

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
// 3. 3D Diorama PerspectiveCamera & EditorCamera Tests
// ==========================================
TEST(PerspectiveCameraTest, OrthonormalBasisAndGroundRaycasting) {
    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({6.5f, 0.0f, 7.0f});
    cam.SetDistance(15.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);

    // At Yaw = 0°, ForwardXZ points North (-Z) and RightXZ points East (+X)
    EXPECT_NEAR(cam.GetForwardXZ().x, 0.0f, 1e-5f);
    EXPECT_NEAR(cam.GetForwardXZ().z, -1.0f, 1e-5f);
    EXPECT_NEAR(cam.GetRightXZ().x, 1.0f, 1e-5f);
    EXPECT_NEAR(cam.GetRightXZ().z, 0.0f, 1e-5f);

    // Camera view basis (Right, Up, Forward) must be mutually orthogonal and unit-length
    EXPECT_NEAR(glm::length(cam.GetForward()), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(cam.GetRight()), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(cam.GetUp()), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(cam.GetRight(), cam.GetUp()), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(cam.GetForward(), cam.GetUp()), 0.0f, 1e-5f);
    EXPECT_GT(cam.GetUp().y, 0.0f);

    // Raycast through NDC (0, 0) must strike exact FocalPoint on Y = 0
    glm::vec3 centerHit{0.0f};
    ASSERT_TRUE(cam.RaycastGroundPlane({0.0f, 0.0f}, 0.0f, centerHit));
    EXPECT_NEAR(centerHit.x, 6.5f, 1e-3f);
    EXPECT_NEAR(centerHit.y, 0.0f, 1e-4f);
    EXPECT_NEAR(centerHit.z, 7.0f, 1e-3f);

    // Raycast to the right of screen center (NDC +0.5, 0) must land East (+X) of FocalPoint
    glm::vec3 rightHit{0.0f};
    ASSERT_TRUE(cam.RaycastGroundPlane({0.5f, 0.0f}, 0.0f, rightHit));
    EXPECT_GT(rightHit.x, centerHit.x);

    // Orbiting Yaw to 45° still strikes exact FocalPoint at screen center
    cam.SetYawDegrees(45.0f);
    glm::vec3 angledCenterHit{0.0f};
    ASSERT_TRUE(cam.RaycastGroundPlane({0.0f, 0.0f}, 0.0f, angledCenterHit));
    EXPECT_NEAR(angledCenterHit.x, 6.5f, 1e-3f);
    EXPECT_NEAR(angledCenterHit.z, 7.0f, 1e-3f);
}

TEST(EditorCameraTest, ScrollPanOrbitAndResetUpdateStateDeterministically) {
    Engine::EditorCamera editorCam(16.0f / 9.0f, 15.0f);
    const glm::vec3 defaultFocal = editorCam.GetFocalPoint();
    EXPECT_FLOAT_EQ(editorCam.GetDistance(), 15.0f);
    EXPECT_FLOAT_EQ(editorCam.GetPitchDegrees(), 40.0f);
    EXPECT_FLOAT_EQ(editorCam.GetYawDegrees(), 0.0f);

    // Scrolling up zooms in (shorter 3D camera distance)
    editorCam.OnMouseScroll(2.0f);
    EXPECT_LT(editorCam.GetDistance(), 15.0f);

    // Panning shifts ground-plane focal point on X and Z
    editorCam.Pan({128.0f, -72.0f}, {1280.0f, 720.0f});
    EXPECT_NE(editorCam.GetFocalPoint().x, defaultFocal.x);
    EXPECT_NE(editorCam.GetFocalPoint().z, defaultFocal.z);

    // Orbit updates Yaw and clamped Pitch
    editorCam.Orbit(35.0f, 10.0f);
    EXPECT_FLOAT_EQ(editorCam.GetYawDegrees(), 35.0f);
    EXPECT_FLOAT_EQ(editorCam.GetPitchDegrees(), 50.0f);

    // Reset restores canonical diorama framing
    editorCam.Reset();
    EXPECT_FLOAT_EQ(editorCam.GetDistance(), 15.0f);
    EXPECT_FLOAT_EQ(editorCam.GetYawDegrees(), 0.0f);
    EXPECT_FLOAT_EQ(editorCam.GetPitchDegrees(), 40.0f);
    EXPECT_FLOAT_EQ(editorCam.GetFocalPoint().x, defaultFocal.x);
    EXPECT_FLOAT_EQ(editorCam.GetFocalPoint().z, defaultFocal.z);
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

    auto* heroSprite = hero->AddComponent<Engine::SpriteRenderer>();
    heroSprite->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::CylindricalY);
    heroSprite->SetAlphaCutoff(0.32f);

    hero->AddComponent<Engine::ParticleSystem>(256);

    auto* cam = hero->AddComponent<Engine::CameraComponent>();
    cam->FOV = 36.0f;
    cam->Distance = 18.5f;
    cam->Pitch = 42.0f;
    cam->Yaw = -25.0f;
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

    auto* clonedSprite = clonedHero->GetComponent<Engine::SpriteRenderer>();
    ASSERT_NE(clonedSprite, nullptr);
    EXPECT_EQ(clonedSprite->GetBillboardMode(), Engine::SpriteRenderer::BillboardMode::CylindricalY);
    EXPECT_NEAR(clonedSprite->GetAlphaCutoff(), 0.32f, 1e-5f);

    auto* clonedParticles = clonedHero->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(clonedParticles, nullptr);
    EXPECT_EQ(clonedParticles->GetParticles().size(), 256u);

    auto* clonedCam = clonedHero->GetComponent<Engine::CameraComponent>();
    ASSERT_NE(clonedCam, nullptr);
    EXPECT_NEAR(clonedCam->FOV, 36.0f, 1e-5f);
    EXPECT_NEAR(clonedCam->Distance, 18.5f, 1e-5f);
    EXPECT_NEAR(clonedCam->Pitch, 42.0f, 1e-5f);
    EXPECT_NEAR(clonedCam->Yaw, -25.0f, 1e-5f);
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

#ifdef ENGINE_BUILD_EDITOR
// ==========================================
// 7. Editor Default Workspace & Ini Persistence Tests
// ==========================================
TEST(EditorWorkspaceTest, DefaultWorkspaceDocksAllPanelsAndRoundTripsIni) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ASSERT_NE(ctx, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const std::filesystem::path tempIniPath = std::filesystem::temp_directory_path() / "engine_test_editor_layout.ini";
    Engine::ImGuiLayer::SetIniFilePath(tempIniPath);
    ASSERT_NE(io.IniFilename, nullptr);
    EXPECT_EQ(std::string(io.IniFilename), Engine::ImGuiLayer::GetIniFilePath());

    const ImGuiID dockspaceId = ImHashStr("MainDockSpace");
    EXPECT_EQ(ImGui::DockBuilderGetNode(dockspaceId), nullptr);

    // Build programmatic Default Workspace at 1280x720
    Engine::ImGuiLayer::ApplyDefaultWorkspaceLayout(dockspaceId, 1280.0f, 720.0f);

    ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(dockspaceId);
    ASSERT_NE(rootNode, nullptr);
    EXPECT_TRUE(rootNode->IsSplitNode());

    ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId);
    ASSERT_NE(centralNode, nullptr);

    const char* expectedPanels[] = {
        "Toolbar", "Scene", "Hierarchy", "Inspector",
        "Tile Palette", "Asset Browser", "Profiler", "Console"
    };
    for (const char* panelName : expectedPanels) {
        ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(panelName));
        ASSERT_NE(settings, nullptr) << "Missing window settings for panel: " << panelName;
        EXPECT_NE(settings->DockId, 0u) << "Panel was not docked: " << panelName;
    }

    // Scene must occupy the Central Node
    ImGuiWindowSettings* sceneSettings = ImGui::FindWindowSettingsByID(ImHashStr("Scene"));
    EXPECT_EQ(sceneSettings->DockId, centralNode->ID);

    // Asset Browser and Console must be tabbed together in the same bottom dock node
    ImGuiWindowSettings* assetSettings = ImGui::FindWindowSettingsByID(ImHashStr("Asset Browser"));
    ImGuiWindowSettings* consoleSettings = ImGui::FindWindowSettingsByID(ImHashStr("Console"));
    EXPECT_EQ(assetSettings->DockId, consoleSettings->DockId);

    // Toolbar dock node must enforce NoTabBar and NoResizeY
    ImGuiWindowSettings* toolbarSettings = ImGui::FindWindowSettingsByID(ImHashStr("Toolbar"));
    ImGuiDockNode* toolbarNode = ImGui::DockBuilderGetNode(toolbarSettings->DockId);
    ASSERT_NE(toolbarNode, nullptr);
    EXPECT_NE(toolbarNode->LocalFlags & ImGuiDockNodeFlags_NoTabBar, 0);
    EXPECT_NE(toolbarNode->LocalFlags & ImGuiDockNodeFlags_NoResizeY, 0);

    // Verify .ini serialization round-trip preserves MainDockSpace across sessions
    size_t iniSize = 0;
    const char* serializedIni = ImGui::SaveIniSettingsToMemory(&iniSize);
    ASSERT_NE(serializedIni, nullptr);
    const std::string iniSnapshot(serializedIni, iniSize);
    EXPECT_NE(iniSnapshot.find("[Docking][Data]"), std::string::npos);

    Engine::ImGuiLayer::SetIniFilePath({});
    ImGui::DestroyContext(ctx);

    ImGuiContext* reloadedCtx = ImGui::CreateContext();
    ImGuiIO& reloadedIo = ImGui::GetIO();
    reloadedIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    reloadedIo.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    reloadedIo.DisplaySize = ImVec2(1280.0f, 720.0f);
    reloadedIo.DeltaTime = 1.0f / 60.0f;
    reloadedIo.IniFilename = nullptr;
    ImGui::LoadIniSettingsFromMemory(iniSnapshot.c_str(), iniSnapshot.size());

    // Simulate a live frame + ResetWorkspaceLayout trigger through BeginDockspace/EndDockspace
    ImGui::NewFrame();
    Engine::ImGuiLayer::BeginDockspace();
    const ImGuiID liveDockspaceId = ImGui::GetID("MainDockSpace");
    EXPECT_NE(ImGui::DockBuilderGetNode(liveDockspaceId), nullptr);
    Engine::ImGuiLayer::EndDockspace();
    ImGui::EndFrame();

    Engine::ImGuiLayer::ResetWorkspaceLayout();
    ImGui::NewFrame();
    Engine::ImGuiLayer::BeginDockspace();
    ASSERT_NE(ImGui::DockBuilderGetCentralNode(liveDockspaceId), nullptr);
    Engine::ImGuiLayer::EndDockspace();
    ImGui::EndFrame();

    ImGui::DestroyContext(reloadedCtx);

    std::error_code ec;
    std::filesystem::remove(tempIniPath, ec);
}
#endif

// ==========================================
// 8. Engine::Layer Lifecycle Tests
// ==========================================
TEST(LayerTest, CustomLayerLifecycleCallbacks) {
    class MockLayer : public Engine::Layer {
    public:
        MockLayer() : Engine::Layer("MockEditorLayer") {}
        int attachCount = 0;
        int detachCount = 0;
#ifdef ENGINE_BUILD_EDITOR
        int imguiCount = 0;
#endif
        float accumulatedDt = 0.0f;

        void OnAttach() override { ++attachCount; }
        void OnDetach() override { ++detachCount; }
        void OnUpdate(float deltaTime) override { accumulatedDt += deltaTime; }
#ifdef ENGINE_BUILD_EDITOR
        void OnImGuiRender() override { ++imguiCount; }
#endif
    };

    auto layer = std::make_unique<MockLayer>();
    EXPECT_EQ(layer->GetName(), "MockEditorLayer");

    layer->OnAttach();
    layer->OnUpdate(0.016f);
    layer->OnUpdate(0.032f);
#ifdef ENGINE_BUILD_EDITOR
    layer->OnImGuiRender();
#endif
    layer->OnDetach();

    EXPECT_EQ(layer->attachCount, 1);
    EXPECT_NEAR(layer->accumulatedDt, 0.048f, 1e-5f);
#ifdef ENGINE_BUILD_EDITOR
    EXPECT_EQ(layer->imguiCount, 1);
#endif
    EXPECT_EQ(layer->detachCount, 1);
}

// ==========================================
// 9. Standalone Distribution Startup & Scene Boot Tests
// ==========================================
TEST(BuildConfigurationTest, StandaloneSceneFileStartupAndMissingFileFailure) {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_dist_startup_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    Engine::ResourceManager::SetAssetRoot(tempDir);

    // 1. Missing startup scene must fail cleanly
    auto missingScene = std::make_shared<Engine::Scene>("Missing");
    EXPECT_FALSE(Engine::SceneSerializer(missingScene).Deserialize("nonexistent_startup.scene"));

    // 2. Author a portable startup scene on disk and boot it through the standalone runtime lifecycle
    {
        auto authoredScene = std::make_shared<Engine::Scene>("StandaloneLevel1");
        authoredScene->SetOrthographicSize(5.0f);

        auto* player = authoredScene->CreateGameObject("Player");
        player->GetTransform()->position = {2.0f, 3.0f, 4.0f};
        auto* rb = player->AddComponent<Engine::Rigidbody2D>();
        rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
        auto* bc = player->AddComponent<Engine::BoxCollider2D>();
        bc->size = {0.6f, 0.6f};

        auto* map = authoredScene->CreateGameObject("Map");
        auto* tilemap = map->AddComponent<Engine::Tilemap>(4, 4);
        tilemap->SetTile(1, 1, 2, false);

        ASSERT_TRUE(Engine::SceneSerializer(authoredScene).Serialize("level1.scene"));
    }

    auto activeScene = std::make_shared<Engine::Scene>("Empty");
    ASSERT_TRUE(Engine::SceneSerializer(activeScene).Deserialize("level1.scene"));
    EXPECT_EQ(activeScene->GetName(), "StandaloneLevel1");

    activeScene->OnViewportResize(1920, 1080);
    activeScene->OnRuntimeStart();
    EXPECT_TRUE(activeScene->IsRuntimeRunning());

    // Step simulation without any Framebuffer or ImGui context
    for (int i = 0; i < 10; ++i) {
        activeScene->UpdateRuntime(1.0f / 60.0f);
    }

    Engine::GameObject* loadedPlayer = nullptr;
    for (const auto& go : activeScene->GetGameObjects()) {
        if (go->GetName() == "Player") {
            loadedPlayer = go.get();
            break;
        }
    }
    ASSERT_NE(loadedPlayer, nullptr);
    EXPECT_LT(loadedPlayer->GetTransform()->position.y, 3.0f);

    activeScene->OnRuntimeStop();
    EXPECT_FALSE(activeScene->IsRuntimeRunning());

    Engine::ResourceManager::SetAssetRoot({});
    std::filesystem::remove_all(tempDir, ec);
}




