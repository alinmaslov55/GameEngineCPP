#include <gtest/gtest.h>
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/LightComponents.h"
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

TEST(FrustumCullingTest, PerspectiveFrustumSphereAndAABBCulling) {
    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 0.0f });
    cam.SetDistance(15.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);

    const Engine::Frustum& frustum = cam.GetFrustum();

    // 1. Focal point at (0, 0, 0) is inside the frustum
    EXPECT_TRUE(frustum.IntersectsSphere({ 0.0f, 0.0f, 0.0f }, 0.5f));
    EXPECT_TRUE(frustum.IntersectsAABB({ -0.5f, 0.0f, -0.5f }, { 0.5f, 1.0f, 0.5f }));

    // 2. Point directly behind the camera eye is culled
    const glm::vec3 behindEye = cam.GetPosition() - cam.GetForward() * 5.0f;
    EXPECT_FALSE(frustum.IntersectsSphere(behindEye, 1.0f));
    EXPECT_FALSE(frustum.IntersectsAABB(behindEye - glm::vec3(0.5f), behindEye + glm::vec3(0.5f)));

    // 3. Point far to the East/West outside horizontal FOV is culled
    EXPECT_FALSE(frustum.IntersectsSphere({ 50.0f, 0.0f, 0.0f }, 1.0f));
    EXPECT_FALSE(frustum.IntersectsSphere({ -50.0f, 0.0f, 0.0f }, 1.0f));

    // 4. Point beyond FarClip (100m) along forward ray is culled
    const glm::vec3 beyondFar = cam.GetPosition() + cam.GetForward() * 120.0f;
    EXPECT_FALSE(frustum.IntersectsSphere(beyondFar, 1.0f));
}

TEST(FrustumCullingTest, ShadowOrthographicFrustumAndGroundPlaneVisibleAABB) {
    Engine::DirectionalLightComponent dirLight;
    dirLight.ElevationDegrees = 45.0f;
    dirLight.AzimuthDegrees = -35.0f;

    const glm::vec3 focus = { 6.5f, 0.0f, 6.5f };
    const float orthoRadius = 15.0f;
    const glm::mat4 lightSpaceVP = dirLight.ComputeLightSpaceMatrix(focus, orthoRadius, 2048u);
    const Engine::Frustum shadowFrustum = Engine::Frustum::FromViewProjection(lightSpaceVP);

    // Standee at focus is inside the orthographic shadow frustum
    EXPECT_TRUE(shadowFrustum.IntersectsSphere(focus, 1.0f));
    // Standee 80m away is outside the 15m orthographic shadow frustum
    EXPECT_FALSE(shadowFrustum.IntersectsSphere({ 90.0f, 0.0f, 90.0f }, 1.5f));

    // Ground plane visible AABB bounds focal point across yaw & pitch angles
    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint(focus);
    cam.SetDistance(15.0f);

    for (float pitch : { 20.0f, 40.0f, 70.0f }) {
        for (float yaw : { 0.0f, 45.0f, 135.0f, 270.0f }) {
            cam.SetPitchDegrees(pitch);
            cam.SetYawDegrees(yaw);

            glm::vec2 minXZ{ 0.0f }, maxXZ{ 0.0f };
            EXPECT_TRUE(cam.GetGroundPlaneVisibleAABB(0.0f, minXZ, maxXZ));
            EXPECT_LT(minXZ.x, focus.x);
            EXPECT_GT(maxXZ.x, focus.x);
            EXPECT_LT(minXZ.y, focus.z);
            EXPECT_GT(maxXZ.y, focus.z);
        }
    }
}

} // namespace

