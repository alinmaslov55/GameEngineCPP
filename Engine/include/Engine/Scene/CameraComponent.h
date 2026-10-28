#ifndef SCENE_CAMERACOMPONENT_H
#define SCENE_CAMERACOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Renderer/PerspectiveCamera.h"
#include <cstdint>

namespace Engine {
class CameraComponent : public Component {
public:
    PerspectiveCamera Camera;
    float FOV = 32.0f;
    float Distance = 14.0f;
    float Pitch = 40.0f;
    float Yaw = 0.0f;
    bool FixedAspectRatio = false;

    CameraComponent() : Camera(32.0f, 16.0f / 9.0f, 0.1f, 250.0f) {
        ApplySettings();
    }
    ~CameraComponent() override = default;

    void ApplySettings() {
        Camera.SetFOVDegrees(FOV);
        Camera.SetDistance(Distance);
        Camera.SetPitchDegrees(Pitch);
        Camera.SetYawDegrees(Yaw);
    }

    void SyncFromTransform(const Transform* transform) {
        ApplySettings();
        if (transform) {
            Camera.SetFocalPoint(transform->position);
        }
    }

    void SetViewportSize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) {
            return;
        }
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        Camera.SetAspectRatio(aspectRatio);
        ApplySettings();
    }
};
} // namespace Engine

#endif // SCENE_CAMERACOMPONENT_H
