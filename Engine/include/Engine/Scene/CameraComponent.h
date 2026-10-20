#ifndef SCENE_CAMERACOMPONENT_H
#define SCENE_CAMERACOMPONENT_H

#include "Engine/Scene/Component.h"
#include "Engine/Renderer/OrthographicCamera.h"

namespace Engine {
class CameraComponent : public Component {
public:
    OrthographicCamera Camera;

    // 16:9 aspect ratio camera
    CameraComponent() : Camera(-1.6f, 1.6f, -0.9f, 0.9f) {}
    ~CameraComponent() override = default;
};
} // namespace Engine

#endif // SCENE_CAMERACOMPONENT_H