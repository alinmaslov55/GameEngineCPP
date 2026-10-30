#include <gtest/gtest.h>
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/Transform.h"

namespace {

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

TEST(OrthographicCameraTest, ProjectionAndViewMatrix) {
    Engine::OrthographicCamera ortho(-1.0f, 1.0f, -1.0f, 1.0f);
    EXPECT_EQ(ortho.GetPosition(), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(ortho.GetRotation(), 0.0f);

    ortho.SetPosition({ 2.0f, 3.0f, 0.0f });
    EXPECT_EQ(ortho.GetPosition(), glm::vec3(2.0f, 3.0f, 0.0f));

    ortho.SetRotation(45.0f);
    EXPECT_FLOAT_EQ(ortho.GetRotation(), 45.0f);

    ortho.SetProjection(-2.0f, 2.0f, -2.0f, 2.0f);
    const glm::mat4& vp = ortho.GetViewProjectionMatrix();
    // Deterministic non-zero matrix
    EXPECT_NE(vp[0][0], 0.0f);
}

TEST(CameraComponentTest, SettingsAndTransformSync) {
    Engine::CameraComponent camComp;
    camComp.FOV = 45.0f;
    camComp.Distance = 20.0f;
    camComp.Pitch = 30.0f;
    camComp.Yaw = 15.0f;

    Engine::Transform transform;
    transform.position = { 5.0f, 1.0f, -3.0f };

    camComp.SyncFromTransform(&transform);
    EXPECT_FLOAT_EQ(camComp.Camera.GetFOVDegrees(), 45.0f);
    EXPECT_FLOAT_EQ(camComp.Camera.GetDistance(), 20.0f);
    EXPECT_FLOAT_EQ(camComp.Camera.GetPitchDegrees(), 30.0f);
    EXPECT_FLOAT_EQ(camComp.Camera.GetYawDegrees(), 15.0f);
    EXPECT_EQ(camComp.Camera.GetFocalPoint(), transform.position);

    camComp.SetViewportSize(1920, 1080);
    EXPECT_NEAR(camComp.Camera.GetAspectRatio(), 1920.0f / 1080.0f, 1e-4f);
}

} // namespace

