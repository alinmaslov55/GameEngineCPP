#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/OrthographicCamera.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/LightComponents.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSerializer.h"
#include "Engine/Scene/Transform.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

[[nodiscard]] glm::vec3 UnprojectRayFromNDC(const Engine::PerspectiveCamera& cam, const glm::vec2& ndc) {
    const glm::mat4 invVP = cam.GetInverseViewProjectionMatrix();
    glm::vec4 nearPt = invVP * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
    glm::vec4 farPt  = invVP * glm::vec4(ndc.x, ndc.y,  1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;
    return glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));
}

[[nodiscard]] float RayConeAngleDegrees(const glm::vec3& rayA, const glm::vec3& rayB) {
    return glm::degrees(std::acos(std::clamp(glm::dot(rayA, rayB), -1.0f, 1.0f)));
}

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

TEST(PerspectiveCameraTest, AdaptiveFOVLandscapeAndPortraitRayCones) {
    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({ 0.0f, 0.0f, 0.0f });
    cam.SetDistance(15.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);
    cam.SetPortraitHorizontalFOVDegrees(22.0f);
    cam.SetFOVAdaptationMode(Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz);

    // 1. 16:9 Landscape (1920x1080): Locks 32.0 deg vertical FOV and expands horizontal visibility (Hor+)
    cam.SetAspectRatio(1920.0f / 1080.0f);
    {
        const glm::vec3 rayBottom = UnprojectRayFromNDC(cam, { 0.0f, -1.0f });
        const glm::vec3 rayTop    = UnprojectRayFromNDC(cam, { 0.0f,  1.0f });
        const glm::vec3 rayLeft   = UnprojectRayFromNDC(cam, { -1.0f, 0.0f });
        const glm::vec3 rayRight  = UnprojectRayFromNDC(cam, {  1.0f, 0.0f });

        const float vertAngle  = RayConeAngleDegrees(rayBottom, rayTop);
        const float horizAngle = RayConeAngleDegrees(rayLeft, rayRight);
        const float expectedHoriz = glm::degrees(
            2.0f * std::atan(std::tan(glm::radians(32.0f) * 0.5f) * (1920.0f / 1080.0f)));

        EXPECT_NEAR(cam.GetEffectiveVerticalFOVDegrees(), 32.0f, 1e-4f);
        EXPECT_NEAR(vertAngle, 32.0f, 1e-3f);
        EXPECT_NEAR(horizAngle, expectedHoriz, 1e-3f);
        EXPECT_NEAR(cam.GetEffectiveHorizontalFOVDegrees(), expectedHoriz, 1e-4f);
    }

    // 2. 9:16 Portrait (1080x1920): Locks exact 22.0 deg horizontal ray cone & expands vertical Z sightline (~38.07 deg)
    cam.SetAspectRatio(1080.0f / 1920.0f);
    {
        const glm::vec3 rayLeft   = UnprojectRayFromNDC(cam, { -1.0f, 0.0f });
        const glm::vec3 rayRight  = UnprojectRayFromNDC(cam, {  1.0f, 0.0f });
        const glm::vec3 rayBottom = UnprojectRayFromNDC(cam, { 0.0f, -1.0f });
        const glm::vec3 rayTop    = UnprojectRayFromNDC(cam, { 0.0f,  1.0f });

        const float horizAngle = RayConeAngleDegrees(rayLeft, rayRight);
        const float vertAngle  = RayConeAngleDegrees(rayBottom, rayTop);
        const float expectedVert = glm::degrees(
            2.0f * std::atan(std::tan(glm::radians(22.0f) * 0.5f) / (1080.0f / 1920.0f)));

        EXPECT_NEAR(horizAngle, 22.0f, 1e-3f);
        EXPECT_NEAR(cam.GetEffectiveHorizontalFOVDegrees(), 22.0f, 1e-4f);
        EXPECT_NEAR(vertAngle, expectedVert, 1e-3f);
        EXPECT_NEAR(vertAngle, 38.1269f, 1e-3f);
        EXPECT_NEAR(cam.GetEffectiveVerticalFOVDegrees(), expectedVert, 1e-4f);
    }

    // 3. 10:16 Portrait (1200x1920): Preserves exact 22.0 deg horizontal cone angle
    cam.SetAspectRatio(1200.0f / 1920.0f);
    {
        const glm::vec3 rayLeft  = UnprojectRayFromNDC(cam, { -1.0f, 0.0f });
        const glm::vec3 rayRight = UnprojectRayFromNDC(cam, {  1.0f, 0.0f });
        EXPECT_NEAR(RayConeAngleDegrees(rayLeft, rayRight), 22.0f, 1e-3f);
    }

    // 4. 1:1 Square boundary (aspect == 1.0f >= 1.0f): Locks 32.0 deg vertical FOV
    cam.SetAspectRatio(1.0f);
    {
        const glm::vec3 rayBottom = UnprojectRayFromNDC(cam, { 0.0f, -1.0f });
        const glm::vec3 rayTop    = UnprojectRayFromNDC(cam, { 0.0f,  1.0f });
        const glm::vec3 rayLeft   = UnprojectRayFromNDC(cam, { -1.0f, 0.0f });
        const glm::vec3 rayRight  = UnprojectRayFromNDC(cam, {  1.0f, 0.0f });
        EXPECT_NEAR(RayConeAngleDegrees(rayBottom, rayTop), 32.0f, 1e-3f);
        EXPECT_NEAR(RayConeAngleDegrees(rayLeft, rayRight), 32.0f, 1e-3f);
    }

    // 5. Legacy FixedVertical opt-out in 9:16 Portrait locks 32.0 deg vFOV and narrows hFOV (~18.43 deg)
    cam.SetFOVAdaptationMode(Engine::FOVAdaptationMode::FixedVertical);
    cam.SetAspectRatio(1080.0f / 1920.0f);
    {
        const glm::vec3 rayBottom = UnprojectRayFromNDC(cam, { 0.0f, -1.0f });
        const glm::vec3 rayTop    = UnprojectRayFromNDC(cam, { 0.0f,  1.0f });
        const glm::vec3 rayLeft   = UnprojectRayFromNDC(cam, { -1.0f, 0.0f });
        const glm::vec3 rayRight  = UnprojectRayFromNDC(cam, {  1.0f, 0.0f });
        EXPECT_NEAR(RayConeAngleDegrees(rayBottom, rayTop), 32.0f, 1e-3f);
        EXPECT_LT(RayConeAngleDegrees(rayLeft, rayRight), 19.0f);
    }
}

TEST(CameraComponentTest, AdaptiveFOVPortraitViewportAndSerializerRoundTrip) {
    auto srcScene = std::make_shared<Engine::Scene>("AdaptiveFovTest");
    srcScene->GetCamera().SetFOVDegrees(34.0f);
    srcScene->GetCamera().SetPortraitHorizontalFOVDegrees(24.0f);
    srcScene->GetCamera().SetFOVAdaptationMode(Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz);

    auto* camEntity = srcScene->CreateGameObject("PortraitCamera");
    auto* camComp = camEntity->AddComponent<Engine::CameraComponent>();
    camComp->FOV = 36.0f;
    camComp->PortraitHorizontalFOV = 22.0f;
    camComp->FOVMode = Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz;
    camComp->SetViewportSize(1080, 1920);
    EXPECT_NEAR(camComp->Camera.GetEffectiveHorizontalFOVDegrees(), 22.0f, 1e-4f);
    EXPECT_GT(camComp->Camera.GetEffectiveVerticalFOVDegrees(), 37.5f);

    const std::filesystem::path tempScenePath =
        std::filesystem::temp_directory_path() / "cam01_adaptive_fov_roundtrip.scene";
    std::filesystem::remove(tempScenePath);

    ASSERT_TRUE(Engine::SceneSerializer(srcScene).Serialize(tempScenePath.string()));

    auto dstScene = std::make_shared<Engine::Scene>("Loaded");
    ASSERT_TRUE(Engine::SceneSerializer(dstScene).Deserialize(tempScenePath.string()));

    EXPECT_FLOAT_EQ(dstScene->GetCamera().GetFOVDegrees(), 34.0f);
    EXPECT_FLOAT_EQ(dstScene->GetCamera().GetPortraitHorizontalFOVDegrees(), 24.0f);
    EXPECT_EQ(dstScene->GetCamera().GetFOVAdaptationMode(),
              Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz);

    dstScene->OnViewportResize(1080, 1920);
    EXPECT_NEAR(dstScene->GetCamera().GetEffectiveHorizontalFOVDegrees(), 24.0f, 1e-4f);

    Engine::GameObject* loadedEntity = dstScene->GetGameObjects().front().get();
    auto* loadedComp = loadedEntity->GetComponent<Engine::CameraComponent>();
    ASSERT_NE(loadedComp, nullptr);
    EXPECT_FLOAT_EQ(loadedComp->FOV, 36.0f);
    EXPECT_FLOAT_EQ(loadedComp->PortraitHorizontalFOV, 22.0f);
    EXPECT_EQ(loadedComp->FOVMode, Engine::FOVAdaptationMode::LandscapeVert_PortraitHorz);
    EXPECT_NEAR(loadedComp->Camera.GetEffectiveHorizontalFOVDegrees(), 22.0f, 1e-4f);

    std::filesystem::remove(tempScenePath);
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

TEST(EditorCameraTest, PortraitPanUsesEffectiveVerticalFOV) {
    Engine::EditorCamera adaptiveCam(9.0f / 16.0f, 15.0f);
    Engine::EditorCamera legacyCam(9.0f / 16.0f, 15.0f);
    legacyCam.GetCamera().SetFOVAdaptationMode(Engine::FOVAdaptationMode::FixedVertical);

    const glm::vec3 startFocal = adaptiveCam.GetFocalPoint();
    adaptiveCam.Pan({ 0.0f, 100.0f }, { 1080.0f, 1920.0f });
    legacyCam.Pan({ 0.0f, 100.0f }, { 1080.0f, 1920.0f });

    const float adaptiveDeltaZ = std::abs(adaptiveCam.GetFocalPoint().z - startFocal.z);
    const float legacyDeltaZ   = std::abs(legacyCam.GetFocalPoint().z - startFocal.z);
    // In portrait mode (38.127 deg effective vFOV > 32.0 deg fixed vFOV), vertical pan covers proportionally more ground
    EXPECT_GT(adaptiveDeltaZ, legacyDeltaZ);
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

TEST(CameraComponentTest, TraumaDecayUnderHitStop) {
    Engine::CameraComponent camComp;
    camComp.ResetToTarget({ 0.0f, 0.0f, 0.0f });

    camComp.TraumaDecay = 1.4f;
    camComp.KickDecayRate = 8.0f;
    camComp.AddTrauma(0.8f, glm::vec2(0.5f, -0.3f));

    EXPECT_FLOAT_EQ(camComp.GetTrauma(), 0.8f);

    // Simulate hit-stop time dilation (effective timescale = 0.02, unscaled dt = 0.016s)
    const float unscaledDt = 0.016f;
    const float scaledDt = unscaledDt * 0.02f; // 50x slowdown in game world

    // Total duration ~1.5 seconds of unscaled real time (94 frames at 60fps)
    for (int frame = 0; frame < 94; ++frame) {
        camComp.UpdateRig(scaledDt, unscaledDt, { 0.0f, 0.0f, 0.0f });
    }

    // After 1.5s unscaled decay at 1.4/s, 0.8 initial trauma and kick must have completely decayed to 0
    EXPECT_NEAR(camComp.GetTrauma(), 0.0f, 1e-4f);
    EXPECT_NEAR(camComp.GetShakeOffsetXZ().x, 0.0f, 1e-4f);
    EXPECT_NEAR(camComp.GetShakeOffsetXZ().y, 0.0f, 1e-4f);
}

TEST(CameraComponentTest, SmoothFollowCriticallyDampedTracking) {
    Engine::CameraComponent camComp;
    camComp.FollowSmoothTime = 0.11f;
    camComp.ResetToTarget({ 0.0f, 0.0f, 0.0f });

    const glm::vec3 targetPos = { 10.0f, 0.0f, 0.0f };
    const float dt = 0.016f;

    float prevX = 0.0f;
    for (int frame = 0; frame < 40; ++frame) { // ~0.64s (~6x smooth time)
        camComp.UpdateRig(dt, dt, targetPos);
        const float currX = camComp.GetSmoothedFocalPoint().x;

        // Monotonic progression without backward oscillation
        EXPECT_GE(currX, prevX);
        // Zero overshoot past target
        EXPECT_LE(currX, 10.0f);
        prevX = currX;
    }

    // Must be within 1% of target position after 0.64s
    EXPECT_GT(camComp.GetSmoothedFocalPoint().x, 9.90f);
}

TEST(CameraComponentTest, AimLookAheadShift) {
    Engine::CameraComponent camComp;
    camComp.AimLookAheadDistance = 2.0f;
    camComp.AimLookAheadSmoothTime = 0.10f;
    camComp.ResetToTarget({ 0.0f, 0.0f, 0.0f });

    // Aim toward +X
    camComp.SetAimDirectionXZ({ 1.0f, 0.0f });

    const float dt = 0.016f;
    for (int frame = 0; frame < 35; ++frame) { // ~0.56s
        camComp.UpdateRig(dt, dt, { 0.0f, 0.0f, 0.0f });
    }

    EXPECT_NEAR(camComp.GetSmoothedAimOffset().x, 2.0f, 0.05f);
    EXPECT_NEAR(camComp.GetSmoothedAimOffset().y, 0.0f, 1e-4f);
    EXPECT_NEAR(camComp.GetLogicalTargetPoint().x, 2.0f, 0.05f);
}

TEST(CameraComponentTest, NonDestructiveTransformPositionInvariance) {
    Engine::CameraComponent camComp;
    Engine::Transform transform;
    transform.position = { 5.0f, 1.2f, -3.4f };
    transform.prevPosition = transform.position;
    transform.rotation = { 10.0f, 20.0f, 0.0f };

    const glm::vec3 originalPos = transform.position;
    const glm::vec3 originalRot = transform.rotation;

    camComp.ResetToTarget(transform.position);
    camComp.AddTrauma(1.0f, glm::vec2(1.0f, -0.8f));

    for (int frame = 0; frame < 30; ++frame) {
        camComp.UpdateRig(0.016f, 0.016f, transform.GetWorldPosition());

        // Transform must NEVER be mutated by camera rig or trauma shake!
        EXPECT_EQ(transform.position, originalPos);
        EXPECT_EQ(transform.prevPosition, originalPos);
        EXPECT_EQ(transform.rotation, originalRot);
    }
}

TEST(CameraComponentTest, DynamicZoomExpansion) {
    Engine::CameraComponent camComp;
    camComp.Distance = 14.0f;
    camComp.DynamicZoomOutMax = 3.0f;
    camComp.DynamicZoomSmoothTime = 0.12f;
    camComp.ResetToTarget({ 0.0f, 0.0f, 0.0f });

    camComp.SetDynamicZoomFactor(1.0f);
    const float dt = 0.016f;

    for (int frame = 0; frame < 40; ++frame) { // ~0.64s
        camComp.UpdateRig(dt, dt, { 0.0f, 0.0f, 0.0f });
    }

    EXPECT_NEAR(camComp.GetCurrentDistance(), 17.0f, 0.05f);
    EXPECT_NEAR(camComp.Camera.GetDistance(), 17.0f, 0.05f);

    // Zoom back in
    camComp.SetDynamicZoomFactor(0.0f);
    for (int frame = 0; frame < 40; ++frame) {
        camComp.UpdateRig(dt, dt, { 0.0f, 0.0f, 0.0f });
    }

    EXPECT_NEAR(camComp.GetCurrentDistance(), 14.0f, 0.05f);
    EXPECT_NEAR(camComp.Camera.GetDistance(), 14.0f, 0.05f);
}

TEST(CameraComponentTest, Perlin1DContinuityAndBounds) {
    for (float t = -10.0f; t <= 10.0f; t += 0.05f) {
        const float val = Engine::CameraComponent::PerlinNoise1D(t);
        EXPECT_LE(std::abs(val), 1.25f);

        const float valNext = Engine::CameraComponent::PerlinNoise1D(t + 0.01f);
        // C1 continuous: delta across 0.01 step must be small
        EXPECT_LE(std::abs(valNext - val), 0.15f);
    }
}

TEST(CameraComponentTest, InactiveCameraSkippedAndMultiPassRenderStepsRigOncePerFrame) {
    Engine::Time::Reset();
    Engine::Time::SetMockDeltaTime(0.016f);
    Engine::Time::Update();

    Engine::Scene scene("MultiPassCameraSyncScene");

    // Inactive camera listed first must be skipped by GatherRenderContext()
    auto* inactiveGo = scene.CreateGameObject("InactiveCamera");
    inactiveGo->GetTransform()->position = { 100.0f, 0.0f, 100.0f };
    auto* inactiveCam = inactiveGo->AddComponent<Engine::CameraComponent>();
    inactiveCam->SmoothFollow = false;
    inactiveCam->ResetToTarget(inactiveGo->GetTransform()->position);
    inactiveGo->SetActive(false);

    // Active gameplay camera listed second
    auto* activeGo = scene.CreateGameObject("ActiveCamera");
    activeGo->GetTransform()->position = { 0.0f, 0.0f, 0.0f };
    auto* activeCam = activeGo->AddComponent<Engine::CameraComponent>();
    activeCam->FollowSmoothTime = 0.11f;
    activeCam->TraumaDecay = 1.4f;
    activeCam->ResetToTarget({ 0.0f, 0.0f, 0.0f });
    activeCam->AddTrauma(0.8f);

    // Move target slightly so smooth-follow would advance if stepped multiple times
    activeGo->GetTransform()->position = { 10.0f, 0.0f, 0.0f };

    // Frame 1: Call GatherRenderContext() 3 times within the same Time::GetFrameCount()
    const auto ctxPass1 = scene.GetRenderer().GatherRenderContext(scene);
    EXPECT_EQ(ctxPass1.activeCam, &activeCam->Camera);
    const float expectedAfterOneStep = 0.8f - 1.4f * 0.016f;
    EXPECT_NEAR(activeCam->GetTrauma(), expectedAfterOneStep, 1e-5f);
    const glm::vec3 smoothedAfterPass1 = activeCam->GetSmoothedFocalPoint();
    EXPECT_GT(smoothedAfterPass1.x, 0.0f);

    const auto ctxPass2 = scene.GetRenderer().GatherRenderContext(scene);
    const auto ctxPass3 = scene.GetRenderer().GatherRenderContext(scene);
    EXPECT_EQ(ctxPass2.activeCam, &activeCam->Camera);
    EXPECT_EQ(ctxPass3.activeCam, &activeCam->Camera);

    // Rig state must NOT double-step or double-decay trauma on extra passes in the same frame
    EXPECT_FLOAT_EQ(activeCam->GetTrauma(), expectedAfterOneStep);
    EXPECT_EQ(activeCam->GetSmoothedFocalPoint(), smoothedAfterPass1);

    // Frame 2: Advancing Time::Update() allows exactly 1 more step
    Engine::Time::Update();
    scene.GetRenderer().GatherRenderContext(scene);
    const float expectedAfterTwoSteps = 0.8f - 2.0f * 1.4f * 0.016f;
    EXPECT_NEAR(activeCam->GetTrauma(), expectedAfterTwoSteps, 1e-5f);
    EXPECT_GT(activeCam->GetSmoothedFocalPoint().x, smoothedAfterPass1.x);

    Engine::Time::Reset();
}

} // namespace

