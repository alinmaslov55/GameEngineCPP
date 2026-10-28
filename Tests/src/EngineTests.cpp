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
#include "Engine/Renderer/PostProcessor.h"
#include "Engine/Renderer/ShadowMap.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/Component.h"
#include "Engine/Scene/FoliageWindComponents.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/PostProcessComponent.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/TerrainComponent.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/Utils/CutoutProcessor.h"
#include "Engine/Utils/Logger.h"

#include <stb_image_write.h>

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

    // Entity 1: Tilemap with mixed walkability + coexisting Splatmap TerrainComponent
    constexpr uint64_t kMapUUID = 111111111111ULL;
    auto* mapEntity = sourceScene->CreateGameObjectWithUUID(kMapUUID, "WorldMap");
    auto* map = mapEntity->AddComponent<Engine::Tilemap>(4, 3);
    map->SetTile(0, 0, 1, true);
    map->SetTile(1, 2, 2, false);
    map->SetTile(3, 1, 3, false);

    auto* terrain = mapEntity->AddComponent<Engine::TerrainComponent>(12.0f, 10.0f, 32u, 32u);
    terrain->SetBlendSharpness(0.74f);
    terrain->SetEdgeDarkening(0.22f);
    terrain->SetLayerTilingScale(2u, 0.62f);
    terrain->PaintWorldBrush(6.0f, 5.0f, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 2u, 2.5f, 0.5f, 1.0f);
    const glm::u8vec4 expectedCenterWeights = terrain->GetSplatTexel(16u, 16u);
    EXPECT_EQ(expectedCenterWeights.b, 255u);

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
    heroSprite->SetCastShadows(false);
    heroSprite->SetReceiveLighting(true);
    heroSprite->SetEmissive(0.65f);
    heroSprite->SetWindSwayWeight(0.78f);
    heroSprite->SetWindPhaseOffset(1.85f);

    auto* heroSquash = hero->AddComponent<Engine::SquashStretchComponent>();
    heroSquash->Stiffness = 210.0f;
    heroSquash->Damping = 16.5f;
    heroSquash->WalkTiltDegrees = 6.2f;
    heroSquash->JumpLaunchStretch = 2.65f;

    auto* heroParticles = hero->AddComponent<Engine::ParticleSystem>(256);
    heroParticles->AutoEmit = true;
    heroParticles->EmitRate = 12.5f;
    heroParticles->SpawnOffset = { 0.1f, 1.2f, -0.3f };
    heroParticles->EmissiveParticles = true;

    auto* heroPointLight = hero->AddComponent<Engine::PointLightComponent>();
    heroPointLight->Color = { 1.0f, 0.55f, 0.22f };
    heroPointLight->Intensity = 2.15f;
    heroPointLight->Radius = 5.25f;
    heroPointLight->Falloff = 1.65f;
    heroPointLight->HeightOffset = 0.95f;
    heroPointLight->Flicker = true;
    heroPointLight->FlickerSpeed = 9.0f;
    heroPointLight->FlickerAmount = 0.22f;

    auto* cam = hero->AddComponent<Engine::CameraComponent>();
    cam->FOV = 36.0f;
    cam->Distance = 18.5f;
    cam->Pitch = 42.0f;
    cam->Yaw = -25.0f;
    cam->FixedAspectRatio = true;

    // Entity 3: Multiple UIPanels + DirectionalLightComponent on HUD/Environment GameObject
    auto* hud = sourceScene->CreateGameObject("HUD");
    hud->AddComponent<Engine::UIPanel>(glm::vec2{20.0f, 40.0f}, glm::vec2{200.0f, 30.0f}, glm::vec4{0.2f, 0.3f, 0.4f, 0.9f});
    hud->AddComponent<Engine::UIPanel>(glm::vec2{24.0f, 44.0f}, glm::vec2{120.0f, 22.0f}, glm::vec4{0.1f, 0.9f, 0.2f, 1.0f});
    auto* sun = hud->AddComponent<Engine::DirectionalLightComponent>();
    sun->ElevationDegrees = 34.5f;
    sun->AzimuthDegrees = -52.0f;
    sun->SunColor = { 1.0f, 0.86f, 0.68f };
    sun->SunIntensity = 1.15f;
    sun->AmbientColor = { 0.32f, 0.38f, 0.64f };
    sun->AmbientIntensity = 0.58f;
    sun->CastShadows = true;
    sun->ShadowSoftness = 1.85f;
    sun->ShadowBias = 0.0024f;

    auto* pp = hud->AddComponent<Engine::PostProcessComponent>();
    pp->ApplyPreset(Engine::PostProcessComponent::Preset::BloodMoonRitual);
    pp->Exposure = 0.24f;
    pp->BloomIntensity = 0.67f;
    pp->BloomScatter = 0.81f;
    pp->VignetteIntensity = 0.44f;

    auto* windZone = hud->AddComponent<Engine::WindComponent>();
    windZone->ApplyPreset(Engine::WindComponent::Preset::AutumnGusts);
    windZone->PlayerInteractionRadius = 1.65f;
    windZone->PlayerPushStrength = 0.46f;

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

    auto* clonedTerrain = clonedMapObj->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(clonedTerrain, nullptr);
    EXPECT_NEAR(clonedTerrain->GetWidth(), 12.0f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetDepth(), 10.0f, 1e-5f);
    EXPECT_EQ(clonedTerrain->GetSplatWidth(), 32u);
    EXPECT_EQ(clonedTerrain->GetSplatHeight(), 32u);
    EXPECT_NEAR(clonedTerrain->GetBlendSharpness(), 0.74f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetEdgeDarkening(), 0.22f, 1e-5f);
    EXPECT_NEAR(clonedTerrain->GetLayer(2u).TilingScale, 0.62f, 1e-5f);
    EXPECT_EQ(clonedTerrain->GetSplatTexel(16u, 16u), expectedCenterWeights);

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
    EXPECT_FALSE(clonedSprite->GetCastShadows());
    EXPECT_TRUE(clonedSprite->GetReceiveLighting());
    EXPECT_NEAR(clonedSprite->GetEmissive(), 0.65f, 1e-5f);
    EXPECT_NEAR(clonedSprite->GetWindSwayWeight(), 0.78f, 1e-5f);
    EXPECT_NEAR(clonedSprite->GetWindPhaseOffset(), 1.85f, 1e-5f);

    auto* clonedSquash = clonedHero->GetComponent<Engine::SquashStretchComponent>();
    ASSERT_NE(clonedSquash, nullptr);
    EXPECT_NEAR(clonedSquash->Stiffness, 210.0f, 1e-5f);
    EXPECT_NEAR(clonedSquash->Damping, 16.5f, 1e-5f);
    EXPECT_NEAR(clonedSquash->WalkTiltDegrees, 6.2f, 1e-5f);
    EXPECT_NEAR(clonedSquash->JumpLaunchStretch, 2.65f, 1e-5f);

    auto* clonedParticles = clonedHero->GetComponent<Engine::ParticleSystem>();
    ASSERT_NE(clonedParticles, nullptr);
    EXPECT_EQ(clonedParticles->GetParticles().size(), 256u);
    EXPECT_TRUE(clonedParticles->AutoEmit);
    EXPECT_NEAR(clonedParticles->EmitRate, 12.5f, 1e-5f);
    EXPECT_NEAR(clonedParticles->SpawnOffset.y, 1.2f, 1e-5f);
    EXPECT_TRUE(clonedParticles->EmissiveParticles);

    auto* clonedPointLight = clonedHero->GetComponent<Engine::PointLightComponent>();
    ASSERT_NE(clonedPointLight, nullptr);
    EXPECT_NEAR(clonedPointLight->Color.g, 0.55f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Intensity, 2.15f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Radius, 5.25f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->Falloff, 1.65f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->HeightOffset, 0.95f, 1e-5f);
    EXPECT_TRUE(clonedPointLight->Flicker);
    EXPECT_NEAR(clonedPointLight->FlickerSpeed, 9.0f, 1e-5f);
    EXPECT_NEAR(clonedPointLight->FlickerAmount, 0.22f, 1e-5f);

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

    auto* clonedSun = clonedHud->GetComponent<Engine::DirectionalLightComponent>();
    ASSERT_NE(clonedSun, nullptr);
    EXPECT_NEAR(clonedSun->ElevationDegrees, 34.5f, 1e-5f);
    EXPECT_NEAR(clonedSun->AzimuthDegrees, -52.0f, 1e-5f);
    EXPECT_NEAR(clonedSun->SunColor.b, 0.68f, 1e-5f);
    EXPECT_NEAR(clonedSun->SunIntensity, 1.15f, 1e-5f);
    EXPECT_NEAR(clonedSun->AmbientColor.b, 0.64f, 1e-5f);
    EXPECT_NEAR(clonedSun->AmbientIntensity, 0.58f, 1e-5f);
    EXPECT_TRUE(clonedSun->CastShadows);
    EXPECT_NEAR(clonedSun->ShadowSoftness, 1.85f, 1e-5f);
    EXPECT_NEAR(clonedSun->ShadowBias, 0.0024f, 1e-5f);

    auto* clonedPP = clonedHud->GetComponent<Engine::PostProcessComponent>();
    ASSERT_NE(clonedPP, nullptr);
    EXPECT_TRUE(clonedPP->Enabled);
    EXPECT_TRUE(clonedPP->BloomEnabled);
    EXPECT_EQ(clonedPP->ToneMapping, Engine::PostProcessComponent::ToneMapMode::ACESFilmic);
    EXPECT_NEAR(clonedPP->Exposure, 0.24f, 1e-5f);
    EXPECT_NEAR(clonedPP->BloomIntensity, 0.67f, 1e-5f);
    EXPECT_NEAR(clonedPP->BloomScatter, 0.81f, 1e-5f);
    EXPECT_NEAR(clonedPP->Contrast, 1.16f, 1e-5f);
    EXPECT_NEAR(clonedPP->VignetteIntensity, 0.44f, 1e-5f);
    EXPECT_NEAR(clonedPP->ShadowTint.r, 0.88f, 1e-5f);
    EXPECT_NEAR(clonedPP->HighlightTint.r, 1.10f, 1e-5f);

    auto* clonedWind = clonedHud->GetComponent<Engine::WindComponent>();
    ASSERT_NE(clonedWind, nullptr);
    EXPECT_TRUE(clonedWind->Enabled);
    EXPECT_NEAR(clonedWind->Strength, 0.28f, 1e-5f);
    EXPECT_NEAR(clonedWind->Gustiness, 0.82f, 1e-5f);
    EXPECT_NEAR(clonedWind->PlayerInteractionRadius, 1.65f, 1e-5f);
    EXPECT_NEAR(clonedWind->PlayerPushStrength, 0.46f, 1e-5f);

    // 2. Test Disk File Round Trip (including external PNG splatmap persistence)
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_serializer_terrain_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path prevAssetRoot = Engine::ResourceManager::GetAssetRoot();
    Engine::ResourceManager::SetAssetRoot(tempDir);

    ASSERT_TRUE(sourceSerializer.Serialize("roundtrip.scene"));

    auto diskScene = std::make_shared<Engine::Scene>();
    Engine::SceneSerializer diskSerializer(diskScene);
    ASSERT_TRUE(diskSerializer.Deserialize("roundtrip.scene"));
    EXPECT_EQ(diskScene->GetName(), "TestSerializedLevel");
    ASSERT_EQ(diskScene->GetGameObjects().size(), 3u);

    auto* diskTerrain = diskScene->GetGameObjects()[0]->GetComponent<Engine::TerrainComponent>();
    ASSERT_NE(diskTerrain, nullptr);
    EXPECT_EQ(diskTerrain->GetSplatTexel(16u, 16u), expectedCenterWeights);
    EXPECT_FALSE(diskTerrain->HasUnsavedMemoryEdits());

    Engine::ResourceManager::SetAssetRoot(prevAssetRoot);
    std::filesystem::remove_all(tempDir, ec);
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

// ==========================================
// 10. Phase 2 Terrain Splatmap Blending & PNG Persistence Tests
// ==========================================
TEST(TerrainSplatmapTest, InitializationRadialFalloffAndWeightNormalization) {
    Engine::TerrainComponent terrain(16.0f, 16.0f, 64u, 64u);

    // 1. Default initialization fills 100% Layer 0 (Meadow Grass: R=255, G=0, B=0, A=0)
    EXPECT_EQ(terrain.GetSplatTexel(0u, 0u), glm::u8vec4(255, 0, 0, 0));
    EXPECT_EQ(terrain.GetSplatTexel(32u, 32u), glm::u8vec4(255, 0, 0, 0));

    // 2. Paint a soft radial stroke of Layer 1 (Dirt) at world center (8.0, 8.0) with radius 4.0m
    const glm::vec3 origin = { 0.0f, 0.0f, 0.0f };
    const glm::vec3 scale  = { 1.0f, 1.0f, 1.0f };
    terrain.PaintWorldBrush(8.0f, 8.0f, origin, scale, /*layerIndex=*/1u, /*radiusWorld=*/4.0f, /*falloff=*/0.8f, /*strength=*/1.0f);

    const glm::u8vec4 centerTexel  = terrain.GetSplatTexel(32u, 32u); // ~0.18m from center (inside inner plateau)
    const glm::u8vec4 falloffTexel = terrain.GetSplatTexel(42u, 32u); // ~2.6m from center (in smoothstep falloff band)
    const glm::u8vec4 outsideTexel = terrain.GetSplatTexel(58u, 32u); // ~6.6m from center (> 4.0m radius)

    EXPECT_EQ(centerTexel.g, 255u);
    EXPECT_GT(falloffTexel.g, 0u);
    EXPECT_LT(falloffTexel.g, centerTexel.g);
    EXPECT_EQ(outsideTexel, glm::u8vec4(255, 0, 0, 0));

    // 3. Paint overlapping Layer 2 (Cobble) & Layer 3 (Soil) strokes and verify exact R+G+B+A == 255 everywhere
    terrain.PaintWorldSegment({ 4.0f, 4.0f }, { 12.0f, 12.0f }, origin, scale, 2u, 3.0f, 0.65f, 0.75f);
    terrain.PaintWorldBrush(9.5f, 7.0f, origin, scale, 3u, 2.5f, 0.7f, 0.6f);

    for (uint32_t pz = 0; pz < terrain.GetSplatHeight(); ++pz) {
        for (uint32_t px = 0; px < terrain.GetSplatWidth(); ++px) {
            const glm::u8vec4 w = terrain.GetSplatTexel(px, pz);
            const int sum = static_cast<int>(w.r) + static_cast<int>(w.g) + static_cast<int>(w.b) + static_cast<int>(w.a);
            ASSERT_EQ(sum, 255) << "Unnormalized splat texel at (" << px << ", " << pz << ")";
        }
    }
}

TEST(TerrainSplatmapTest, PngSaveAndLoadRoundTripWithoutFlipInversion) {
    Engine::TerrainComponent srcTerrain(14.0f, 14.0f, 32u, 32u);
    // Paint an asymmetric marker in the North-West corner (px=2, pz=3) to catch any vertical/horizontal flip bugs
    srcTerrain.SetSplatTexel(2u, 3u, { 10, 40, 180, 25 });
    const glm::u8vec4 expectedMarker = srcTerrain.GetSplatTexel(2u, 3u);

    const std::filesystem::path tempPng = std::filesystem::temp_directory_path() / "engine_splat_flip_check.png";
    ASSERT_TRUE(srcTerrain.SaveSplatmapToPNG(tempPng.string()));
    EXPECT_FALSE(srcTerrain.HasUnsavedMemoryEdits());

    Engine::TerrainComponent loadedTerrain(14.0f, 14.0f, 8u, 8u);
    ASSERT_TRUE(loadedTerrain.LoadSplatmapFromPNG(tempPng.string()));
    EXPECT_EQ(loadedTerrain.GetSplatWidth(), 32u);
    EXPECT_EQ(loadedTerrain.GetSplatHeight(), 32u);
    EXPECT_EQ(loadedTerrain.GetSplatTexel(2u, 3u), expectedMarker);
    EXPECT_EQ(loadedTerrain.GetSplatTexel(2u, 28u), glm::u8vec4(255, 0, 0, 0));

    std::error_code ec;
    std::filesystem::remove(tempPng, ec);
}

// ==========================================
// 11. Phase 3 Dynamic 2.5D Lighting & Cutout Shadow Math Tests
// ==========================================
TEST(LightingMathTest, DirectionalBasisTexelSnappingAndPointLightFlicker) {
    Engine::DirectionalLightComponent dirLight;

    // 1. Verify unit-length downward sun direction & orthogonal horizontal standee shadow basis across 360° azimuths
    const float testAzimuths[] = { -180.0f, -90.0f, -42.0f, 0.0f, 65.0f, 135.0f };
    for (float azim : testAzimuths) {
        dirLight.ElevationDegrees = 40.0f;
        dirLight.AzimuthDegrees = azim;

        const glm::vec3 sunDir = dirLight.GetLightDirection();
        EXPECT_NEAR(glm::length(sunDir), 1.0f, 1e-5f);
        EXPECT_LT(sunDir.y, -0.5f); // Points downward onto Y=0 diorama floor

        const glm::vec3 sunRightXZ = dirLight.GetSunRightXZ();
        EXPECT_NEAR(glm::length(sunRightXZ), 1.0f, 1e-5f);
        EXPECT_NEAR(sunRightXZ.y, 0.0f, 1e-6f);
        // Standee shadow plane right-axis must be strictly perpendicular to horizontal sun rays
        EXPECT_NEAR(glm::dot(sunRightXZ, glm::vec3(sunDir.x, 0.0f, sunDir.z)), 0.0f, 1e-5f);
    }

    // 2. Verify texel-snapped LightSpaceMatrix eliminates sub-texel shadow edge shimmer during camera pan:
    //    For ANY camera pan, shadow-map UV coordinates of a static world point may only shift by exact
    //    integer texel increments (1 / shadowRes), and micro-pans inside a texel cell shift by 0 texels.
    dirLight.ElevationDegrees = 42.0f;
    dirLight.AzimuthDegrees = -38.0f;
    const float orthoRadius = 15.0f;
    const uint32_t shadowRes = 2048u;
    const float texelSizeWorld = (2.0f * orthoRadius) / static_cast<float>(shadowRes);

    // Pick a focus point guaranteed to sit near the center of its light-space texel cell
    const glm::vec3 snappedOriginFocus = { 0.0f, 0.0f, 0.0f };
    const glm::vec3 cellCenterFocus = snappedOriginFocus + dirLight.GetSunRightXZ() * (0.5f * texelSizeWorld);
    const glm::vec3 microPannedFocus = cellCenterFocus + dirLight.GetSunRightXZ() * (0.15f * texelSizeWorld);

    const glm::mat4 matA = dirLight.ComputeLightSpaceMatrix(cellCenterFocus, orthoRadius, shadowRes);
    const glm::mat4 matB = dirLight.ComputeLightSpaceMatrix(microPannedFocus, orthoRadius, shadowRes);
    const glm::vec4 clipA = matA * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
    const glm::vec4 clipB = matB * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
    EXPECT_NEAR(clipA.x, clipB.x, 1e-5f);
    EXPECT_NEAR(clipA.y, clipB.y, 1e-5f);

    // Arbitrary fractional world pans must always quantize to exact integer texel steps in shadow UV [0, 1]
    const glm::vec3 arbitraryPans[] = {
        {  0.371f, 0.0f,  0.829f },
        { -1.413f, 0.0f,  0.617f },
        {  2.104f, 0.0f, -1.732f }
    };
    const glm::vec2 uvBase = glm::vec2(clipA.x, clipA.y) * 0.5f + 0.5f;
    for (const auto& panOffset : arbitraryPans) {
        const glm::mat4 matPanned = dirLight.ComputeLightSpaceMatrix(cellCenterFocus + panOffset, orthoRadius, shadowRes);
        const glm::vec4 clipPanned = matPanned * glm::vec4(6.5f, 0.0f, 5.5f, 1.0f);
        const glm::vec2 uvPanned = glm::vec2(clipPanned.x, clipPanned.y) * 0.5f + 0.5f;
        const glm::vec2 texelShift = (uvPanned - uvBase) * static_cast<float>(shadowRes);
        EXPECT_NEAR(texelShift.x, std::round(texelShift.x), 1e-3f);
        EXPECT_NEAR(texelShift.y, std::round(texelShift.y), 1e-3f);
    }

    // 3. PointLightComponent steady vs flickering evaluation & phase decorrelation
    Engine::PointLightComponent brazierLight;
    brazierLight.Intensity = 2.0f;
    brazierLight.Flicker = false;
    EXPECT_NEAR(brazierLight.EvaluateEffectiveIntensity(1.25f, 42u), 2.0f, 1e-6f);

    brazierLight.Flicker = true;
    brazierLight.FlickerSpeed = 8.0f;
    brazierLight.FlickerAmount = 0.20f;
    const float t0 = brazierLight.EvaluateEffectiveIntensity(0.15f, 101u);
    const float t1 = brazierLight.EvaluateEffectiveIntensity(0.55f, 101u);
    const float seedB = brazierLight.EvaluateEffectiveIntensity(0.15f, 202u);
    EXPECT_NE(t0, t1);
    EXPECT_NE(t0, seedB); // Distinct entities do not pulse in robotic lockstep
    EXPECT_GE(t0, 2.0f * 0.80f - 1e-4f);
    EXPECT_LE(t0, 2.0f * 1.20f + 1e-4f);

    // 4. Headless ShadowMap lifecycle safety (no OpenGL context active in unit test runner)
    Engine::ShadowMap::Init(2048);
    EXPECT_FALSE(Engine::ShadowMap::IsValid());
    EXPECT_EQ(Engine::ShadowMap::GetResolution(), 2048u);
    Engine::ShadowMap::Shutdown();
}

// ==========================================
// 12. Phase 4 HDR Post-Processing (Bloom Prefilter, Tone Mapping & Presets) Tests
// ==========================================
TEST(PostProcessMathTest, SoftKneePrefilterToneMappingAndHeadlessLifecycle) {
    Engine::PostProcessComponent pp;
    pp.BloomThreshold = 1.0f;
    pp.BloomKnee = 0.4f;

    // 1. Quadratic soft-knee prefilter: zero below (Threshold - Knee = 0.6), smooth C1 curve in [0.6, 1.4],
    //    and exact linear excess (C - Threshold) above (Threshold + Knee = 1.4)
    const glm::vec3 belowKnee = pp.EvaluateSoftKneePrefilter({ 0.55f, 0.40f, 0.20f });
    EXPECT_NEAR(belowKnee.r, 0.0f, 1e-6f);
    EXPECT_NEAR(belowKnee.g, 0.0f, 1e-6f);
    EXPECT_NEAR(belowKnee.b, 0.0f, 1e-6f);

    const glm::vec3 atThreshold = pp.EvaluateSoftKneePrefilter({ 1.0f, 1.0f, 1.0f });
    // At brightness == Threshold, quadratic soft knee evaluates to 0.25 * Knee = 0.10
    EXPECT_NEAR(atThreshold.r, 0.10f, 1e-5f);

    const glm::vec3 atUpperKnee = pp.EvaluateSoftKneePrefilter({ 1.4f, 0.7f, 0.0f });
    // At brightness == Threshold + Knee = 1.4, excess is 0.4 -> scaled proportionally across channels
    EXPECT_NEAR(atUpperKnee.r, 0.4f, 1e-5f);
    EXPECT_NEAR(atUpperKnee.g, 0.2f, 1e-5f);

    const glm::vec3 brightHDR = pp.EvaluateSoftKneePrefilter({ 3.0f, 1.5f, 0.0f });
    // Above knee (3.0 > 1.4): contribution is (3.0 - 1.0) / 3.0 = 2/3
    EXPECT_NEAR(brightHDR.r, 2.0f, 1e-5f);
    EXPECT_NEAR(brightHDR.g, 1.0f, 1e-5f);

    // 2. Tone mapping monotonicity, [0, 1] bounds & crimson hue preservation (StorybookNeutral vs ACESFilmic)
    const Engine::PostProcessComponent::ToneMapMode modes[] = {
        Engine::PostProcessComponent::ToneMapMode::ACESFilmic,
        Engine::PostProcessComponent::ToneMapMode::StorybookNeutral,
        Engine::PostProcessComponent::ToneMapMode::Reinhard
    };
    for (auto mode : modes) {
        pp.ToneMapping = mode;
        const glm::vec3 zeroOut = pp.ApplyToneMapCPU({ 0.0f, 0.0f, 0.0f });
        const glm::vec3 lowOut  = pp.ApplyToneMapCPU({ 0.35f, 0.35f, 0.35f });
        const glm::vec3 midOut  = pp.ApplyToneMapCPU({ 1.25f, 1.25f, 1.25f });
        const glm::vec3 highOut = pp.ApplyToneMapCPU({ 8.00f, 8.00f, 8.00f });

        EXPECT_NEAR(zeroOut.r, 0.0f, 1e-5f);
        EXPECT_LT(zeroOut.r, lowOut.r);
        EXPECT_LT(lowOut.r, midOut.r);
        EXPECT_LT(midOut.r, highOut.r);
        EXPECT_LE(highOut.r, 1.0f);
    }

    const glm::vec3 crimsonHDR = { 3.5f, 0.25f, 0.20f };
    pp.ToneMapping = Engine::PostProcessComponent::ToneMapMode::ACESFilmic;
    const glm::vec3 acesCrimson = pp.ApplyToneMapCPU(crimsonHDR);
    pp.ToneMapping = Engine::PostProcessComponent::ToneMapMode::StorybookNeutral;
    const glm::vec3 neutralCrimson = pp.ApplyToneMapCPU(crimsonHDR);

    // StorybookNeutral avoids ACES yellow-orange skew (G - B spread) on vivid crimson flames & runes
    EXPECT_GT(neutralCrimson.r, acesCrimson.r);
    EXPECT_LT(std::abs(neutralCrimson.g - neutralCrimson.b), std::abs(acesCrimson.g - acesCrimson.b));

    // 3. Verify all 5 mood presets populate valid distinct configurations
    pp.ApplyPreset(Engine::PostProcessComponent::Preset::MidnightSanctum);
    EXPECT_LT(pp.Temperature, 0.0f);
    EXPECT_TRUE(pp.VignetteEnabled);

    pp.ApplyPreset(Engine::PostProcessComponent::Preset::CrispNeutral);
    EXPECT_FALSE(pp.VignetteEnabled);
    EXPECT_NEAR(pp.Contrast, 1.0f, 1e-5f);
    EXPECT_NEAR(pp.Saturation, 1.0f, 1e-5f);

    // 4. Headless PostProcessor lifecycle safety (no OpenGL context active in unit test runner)
    Engine::PostProcessor::Init();
    EXPECT_FALSE(Engine::PostProcessor::IsValid());
    Engine::PostProcessor::Shutdown();
}

// ==========================================
// 13. Phase 5 Wind Sway, Proximity Foliage Parting & Squash-and-Stretch Spring Tests
// ==========================================
TEST(WindAndMotionTest, WindFieldRootLockProximityPartingAndVolumePreservingSpring) {
    Engine::WindComponent wind;
    wind.ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);

    // 1. Unit wind direction & traveling spatial phase variation across world XZ
    const glm::vec2 dirXZ = wind.GetWindDirectionXZ();
    EXPECT_NEAR(glm::length(dirXZ), 1.0f, 1e-5f);

    const float phaseOrigin = wind.ComputeSpatialPhase({ 0.0f, 0.0f, 0.0f }, 0.0f);
    const float phaseDownwind = wind.ComputeSpatialPhase({ 4.0f, 0.0f, 3.0f }, 0.0f);
    EXPECT_NE(phaseOrigin, phaseDownwind);

    // 2. Bottom standee/shadow vertices (topWeight == 0.0) MUST stay strictly pinned at 0.0m drift!
    for (float t : { 0.0f, 0.37f, 1.15f, 4.82f }) {
        EXPECT_FLOAT_EQ(wind.EvaluateSwayDisplacement(t, 0.0f, phaseDownwind, 0.25f), 0.0f);
    }

    // Top canopy vertices oscillate smoothly in time when enabled, and freeze wind wave in StillAir
    const float swayT0 = wind.EvaluateSwayDisplacement(0.20f, 0.85f, phaseDownwind);
    const float swayT1 = wind.EvaluateSwayDisplacement(1.05f, 0.85f, phaseDownwind);
    EXPECT_NE(swayT0, swayT1);
    EXPECT_GT(std::abs(swayT0) + std::abs(swayT1), 0.02f);

    wind.ApplyPreset(Engine::WindComponent::Preset::StillAir);
    EXPECT_FALSE(wind.Enabled);
    EXPECT_NEAR(wind.EvaluateSwayDisplacement(0.75f, 0.85f, phaseDownwind, 0.0f), 0.0f, 1e-6f);

    // 3. Interactive proximity parting pushes foliage away along SwayRightAxis inside radius & 0 outside
    wind.ApplyPreset(Engine::WindComponent::Preset::GentleMeadowBreeze);
    const glm::vec3 bushAnchor = { 5.0f, 0.0f, 5.0f };
    const glm::vec3 camRightXZ = { 1.0f, 0.0f, 0.0f };

    // Player slightly to the left of the bush -> bush bends right (+X)
    const float bendRight = wind.EvaluateProximityBend(bushAnchor, { 4.55f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);
    // Player slightly to the right of the bush -> bush bends left (-X)
    const float bendLeft  = wind.EvaluateProximityBend(bushAnchor, { 5.45f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);
    // Player outside interaction radius -> zero bend
    const float bendFar   = wind.EvaluateProximityBend(bushAnchor, { 8.00f, 0.0f, 5.0f }, camRightXZ, 0.80f, 0.5f, 3.0f);

    EXPECT_GT(bendRight, 0.05f);
    EXPECT_LT(bendLeft, -0.05f);
    EXPECT_FLOAT_EQ(bendFar, 0.0f);

    // 4. SquashStretchComponent: volume-preserving spring (sx * sqrt(sy) == 1) & physics scale isolation
    Engine::Scene testScene("MotionTestScene");
    Engine::GameObject* player = testScene.CreateGameObject("Player");
    player->GetTransform()->position = { 2.0f, 0.0f, 3.0f };
    player->GetTransform()->scale = { 1.25f, 1.35f, 1.0f };
    auto* collider = player->AddComponent<Engine::BoxCollider2D>();
    collider->isDynamic = true;
    collider->useGravity = true;
    auto* ss = player->AddComponent<Engine::SquashStretchComponent>();

    ss->TriggerImpulse(2.4f, 8.0f);
    ss->Update(1.0f / 60.0f);

    const glm::vec2 stretched = ss->GetScaleMultiplier();
    EXPECT_GT(stretched.y, 1.01f); // Stretched vertically
    EXPECT_LT(stretched.x, 0.99f); // Narrowed horizontally to conserve volume
    EXPECT_NEAR(stretched.x * std::sqrt(stretched.y), 1.0f, 1e-5f);
    // Underlying physics Transform::scale MUST remain completely untouched!
    EXPECT_FLOAT_EQ(player->GetTransform()->scale.x, 1.25f);
    EXPECT_FLOAT_EQ(player->GetTransform()->scale.y, 1.35f);

    // Settle spring over 1.5 seconds -> converges cleanly back to {1.0, 1.0} and 0 deg tilt
    for (int step = 0; step < 90; ++step) {
        ss->Update(1.0f / 60.0f);
    }
    const glm::vec2 settled = ss->GetScaleMultiplier();
    EXPECT_NEAR(settled.x, 1.0f, 1e-3f);
    EXPECT_NEAR(settled.y, 1.0f, 1e-3f);
    EXPECT_NEAR(ss->GetVisualTiltDegrees(), 0.0f, 1e-2f);

    // Simulate airborne fall -> ground touchdown -> auto-triggers downward landing squash (sy < 1.0)
    player->GetTransform()->position.y = 0.8f;
    ss->Update(1.0f / 60.0f);
    player->GetTransform()->position.y = 0.4f;
    ss->Update(1.0f / 60.0f);
    player->GetTransform()->position.y = 0.0f; // Touchdown!
    ss->Update(1.0f / 60.0f);
    ss->Update(1.0f / 60.0f);
    const glm::vec2 squashed = ss->GetScaleMultiplier();
    EXPECT_LT(squashed.y, 0.99f);
    EXPECT_GT(squashed.x, 1.005f);
    EXPECT_NEAR(squashed.x * std::sqrt(squashed.y), 1.0f, 1e-5f);
}

// ==========================================
// 14. Phase 5 C++ CutoutProcessor (Chroma Key, Interior Holes, Bottom-Flush & RGB Alpha Bleed) Tests
// ==========================================
TEST(CutoutProcessorTest, ChromaGreenAndMagentaKeyingBottomFlushAndAlphaEdgeBleed) {
    // 1. Synthesize a 24x24 #00FF00 chroma-green image containing a crimson banner silhouette [x:8..15, y:4..17]
    //    with a 6px floating green margin at the bottom (y:18..23) AND an interior 2x2 #00FF00 hole at [x:11..12, y:9..10]
    uint32_t w = 24u, h = 24u;
    std::vector<uint8_t> greenImg(static_cast<size_t>(w) * h * 4u, 0u);
    auto setPx = [&](std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        buf[idx + 0] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
        buf[idx + 3] = a;
    };

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            setPx(greenImg, w, x, y, 0u, 255u, 0u, 255u);
        }
    }
    for (uint32_t y = 4; y <= 17; ++y) {
        for (uint32_t x = 8; x <= 15; ++x) {
            setPx(greenImg, w, x, y, 165u, 28u, 36u, 255u); // Deep cult crimson
        }
    }
    // Carve an interior #00FF00 hole inside the crimson banner
    for (uint32_t y = 9; y <= 10; ++y) {
        for (uint32_t x = 11; x <= 12; ++x) {
            setPx(greenImg, w, x, y, 0u, 255u, 0u, 255u);
        }
    }

    Engine::CutoutProcessSettings settings;
    settings.BackgroundMode = Engine::CutoutKeyMode::AutoDetectCorner;
    settings.AutoTrimAndBottomFlush = true;
    settings.HorizontalPaddingPx = 4u;
    settings.TopPaddingPx = 4u;
    settings.AlphaBleedPasses = 3u;

    const auto resGreen = Engine::CutoutProcessor::ProcessRGBAInPlace(greenImg, w, h, settings);
    ASSERT_TRUE(resGreen.Success);
    EXPECT_EQ(resGreen.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaGreen);
    EXPECT_EQ(resGreen.ContentWidth, 8u);   // x: 8..15
    EXPECT_EQ(resGreen.ContentHeight, 14u); // y: 4..17
    EXPECT_EQ(w, 8u + 2u * 4u);             // 16px wide
    EXPECT_EQ(h, 14u + 4u);                 // 18px tall (bottom-flush -> zero bottom padding!)

    auto getPixel = [&](const std::vector<uint8_t>& buf, uint32_t strideW, uint32_t x, uint32_t y) {
        const size_t idx = (static_cast<size_t>(y) * strideW + x) * 4u;
        return glm::u8vec4(buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]);
    };

    // Top-left corner must be fully transparent
    EXPECT_EQ(getPixel(greenImg, w, 0u, 0u).a, 0u);
    // Bottom row (y = h - 1 = 17) inside content [x = 6] MUST be 100% opaque crimson (bottom-flush to UV.y = 0!)
    const glm::u8vec4 bottomCrimson = getPixel(greenImg, w, 6u, h - 1u);
    EXPECT_EQ(bottomCrimson.a, 255u);
    EXPECT_GT(bottomCrimson.r, 150u);
    // Interior hole (originally [11, 9] -> shifted to [4 + (11-8)=7, 4 + (9-4)=9]) MUST be keyed transparent (A == 0)
    const glm::u8vec4 holePx = getPixel(greenImg, w, 7u, 9u);
    EXPECT_EQ(holePx.a, 0u);
    // Alpha-edge RGB bleed: transparent neighbor immediately left of crimson silhouette [x = 3, y = h - 1]
    // keeps A == 0 while inheriting dilated crimson RGB (R > 140, G < 50) instead of #00FF00 green!
    const glm::u8vec4 bledNeighbor = getPixel(greenImg, w, 3u, h - 1u);
    EXPECT_EQ(bledNeighbor.a, 0u);
    EXPECT_GT(bledNeighbor.r, 140u);
    EXPECT_LT(bledNeighbor.g, 50u);

    // 2. Test Chroma-Magenta (#FF00FF) keying + Disk File Round-Trip + ResourceManager::Reload<Texture>
    uint32_t mw = 20u, mh = 20u;
    std::vector<uint8_t> magImg(static_cast<size_t>(mw) * mh * 4u, 0u);
    for (uint32_t y = 0; y < mh; ++y) {
        for (uint32_t x = 0; x < mw; ++x) {
            setPx(magImg, mw, x, y, 255u, 0u, 255u, 255u); // #FF00FF magenta key
        }
    }
    // Paint emerald green grass blades [x:7..12, y:5..14] — must NOT be erased on magenta background!
    for (uint32_t y = 5; y <= 14; ++y) {
        for (uint32_t x = 7; x <= 12; ++x) {
            setPx(magImg, mw, x, y, 36u, 178u, 58u, 255u);
        }
    }

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "engine_cutout_processor_test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path rawMagPng = tempDir / "raw_grass_magenta.png";
    const std::filesystem::path outPng = tempDir / "grass_cutout.png";

    stbi_flip_vertically_on_write(0);
    ASSERT_NE(stbi_write_png(rawMagPng.string().c_str(), static_cast<int>(mw), static_cast<int>(mh), 4, magImg.data(), static_cast<int>(mw * 4u)), 0);

    // Load initial uncropped 20x20 texture into ResourceManager cache
    auto cachedTex = Engine::ResourceManager::Load<Engine::Texture>(rawMagPng.string());
    ASSERT_NE(cachedTex, nullptr);
    EXPECT_EQ(cachedTex->GetWidth(), 20);
    EXPECT_EQ(cachedTex->GetHeight(), 20);

    // Process via CutoutProcessor::ProcessFile directly over rawMagPng & also to outPng
    const auto resFile = Engine::CutoutProcessor::ProcessFile(rawMagPng, outPng, settings);
    ASSERT_TRUE(resFile.Success);
    EXPECT_EQ(resFile.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaMagenta);
    EXPECT_EQ(resFile.OutputWidth, 6u + 2u * 4u); // 14px
    EXPECT_EQ(resFile.OutputHeight, 10u + 4u);    // 14px (bottom-flush)

    ASSERT_TRUE(Engine::CutoutProcessor::ProcessFile(outPng, rawMagPng, settings).Success);
    auto reloadedTex = Engine::ResourceManager::Reload<Engine::Texture>(rawMagPng.string());
    EXPECT_EQ(reloadedTex, cachedTex); // In-place reload preserves existing shared_ptr identity!
    EXPECT_EQ(reloadedTex->GetWidth(), static_cast<int>(resFile.OutputWidth));
    EXPECT_EQ(reloadedTex->GetHeight(), static_cast<int>(resFile.OutputHeight));

    const auto resMag = Engine::CutoutProcessor::ProcessRGBAInPlace(magImg, mw, mh, settings);
    ASSERT_TRUE(resMag.Success);
    EXPECT_EQ(resMag.ResolvedKeyMode, Engine::CutoutKeyMode::ChromaMagenta);
    EXPECT_EQ(getPixel(magImg, mw, 6u, mh - 1u).a, 255u);
    EXPECT_GT(getPixel(magImg, mw, 6u, mh - 1u).g, 160u); // Emerald green preserved intact!

    std::filesystem::remove_all(tempDir, ec);
}






