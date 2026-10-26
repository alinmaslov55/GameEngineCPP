#ifndef SCENE_CAMERACOMPONENT_H
#define SCENE_CAMERACOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/OrthographicCamera.h"
#include <cstdint>

namespace Engine {
class CameraComponent : public Component {
public:
    OrthographicCamera Camera;
    float OrthographicSize = 1.8f;
    bool FixedAspectRatio = false;

    // 16:9 aspect ratio camera
    CameraComponent() : Camera(-1.6f, 1.6f, -0.9f, 0.9f) {}
    ~CameraComponent() override = default;

    void SetViewportSize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return;
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        float orthoLeft   = -OrthographicSize * aspectRatio * 0.5f;
        float orthoRight  =  OrthographicSize * aspectRatio * 0.5f;
        float orthoBottom = -OrthographicSize * 0.5f;
        float orthoTop    =  OrthographicSize * 0.5f;
        Camera.SetProjection(orthoLeft, orthoRight, orthoBottom, orthoTop);
    }
};
} // namespace Engine

#endif // SCENE_CAMERACOMPONENT_H