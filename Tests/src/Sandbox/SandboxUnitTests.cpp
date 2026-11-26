#include <gtest/gtest.h>
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Scripts/PlayerController.h"
#include "Scripts/CameraController.h"
#include "Utils/FilenameUtils.h"

namespace {

TEST(SandboxUnitTest, SanitizeFilenameHandlesInvalidCharsAndReservedNames) {
    EXPECT_EQ(Sandbox::SanitizeFilename("  My:Prop*Name?  .. "), "My_Prop_Name_");
    EXPECT_EQ(Sandbox::SanitizeFilename("a/b\\c<d>e|f\"g"), "a_b_c_d_e_f_g");
    EXPECT_EQ(Sandbox::SanitizeFilename("   ...  "), "GameObject");
    EXPECT_EQ(Sandbox::SanitizeFilename(""), "GameObject");
    EXPECT_EQ(Sandbox::SanitizeFilename("con"), "_con");
    EXPECT_EQ(Sandbox::SanitizeFilename("COM1.prefab"), "_COM1.prefab");
    EXPECT_EQ(Sandbox::SanitizeFilename("NUL"), "_NUL");
    EXPECT_EQ(Sandbox::SanitizeFilename("RitualBrazier_01"), "RitualBrazier_01");
}

TEST(SandboxUnitTest, PlayerControllerAwakeAndIdleFixedUpdate) {
    Engine::Scene scene("PlayerControllerTestScene");
    auto* player = scene.CreateGameObject("Player");
    auto* rb = player->AddComponent<Engine::Rigidbody2D>();
    rb->Type = Engine::Rigidbody2D::BodyType::Dynamic;
    player->AddComponent<Engine::CircleCollider2D>();
    auto* controller = player->AddComponent<PlayerController>();

    scene.OnRuntimeStart();
    EXPECT_EQ(controller->GetMovementInput(), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(controller->m_DustProps.lifeTime, 0.45f);

    rb->SetVelocityXZ(4.0f, 0.0f);
    scene.UpdateRuntime(1.0f / 60.0f);
    // Idle braking reduces velocity toward 0
    EXPECT_LT(glm::length(rb->GetVelocityXZ()), 4.0f);
    scene.OnRuntimeStop();
}

TEST(SandboxUnitTest, CameraControllerDeadzoneAndSmoothFollow) {
    Engine::Scene scene("CameraControllerTestScene");
    auto* player = scene.CreateGameObject("Player");
    player->GetTransform()->position = { 5.0f, 0.0f, -3.0f };

    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 0.0f });

    auto* camCtrl = player->AddComponent<CameraController>();
    camCtrl->camera = &cam;
    camCtrl->deadzone = { 1.4f, 1.0f };
    camCtrl->smoothSpeed = 10.0f;

    camCtrl->Update(0.1f);
    const glm::vec3 focalAfter = cam.GetFocalPoint();
    EXPECT_GT(focalAfter.x, 0.0f);
    EXPECT_LT(focalAfter.z, 0.0f);
    EXPECT_FLOAT_EQ(focalAfter.y, 0.0f);
}

} // namespace
