#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <Engine/Renderer/PerspectiveCamera.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Utils/Logger.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

class CameraController : public Engine::Component {
public:
    Engine::PerspectiveCamera* camera = nullptr;

    glm::vec2 deadzone = { 1.4f, 1.0f };
    float smoothSpeed = 8.0f;

    void Awake() override {
        ENGINE_INFO("CameraController initialized (3D Diorama deadzone: {:.2f}x{:.2f})", deadzone.x, deadzone.y);
    }

    void Update(float deltaTime) override {
        if (!camera) return;

        auto* transform = GetGameObject()->GetTransform();
        if (!transform) return;

        glm::vec3 targetFocal = camera->GetFocalPoint();
        const glm::vec3& playerPos = transform->position;

        const float dx = playerPos.x - targetFocal.x;
        const float dz = playerPos.z - targetFocal.z;

        if (dx > deadzone.x * 0.5f) {
            targetFocal.x = playerPos.x - deadzone.x * 0.5f;
        } else if (dx < -deadzone.x * 0.5f) {
            targetFocal.x = playerPos.x + deadzone.x * 0.5f;
        }

        if (dz > deadzone.y * 0.5f) {
            targetFocal.z = playerPos.z - deadzone.y * 0.5f;
        } else if (dz < -deadzone.y * 0.5f) {
            targetFocal.z = playerPos.z + deadzone.y * 0.5f;
        }

        targetFocal.y = 0.0f;

        const float alpha = 1.0f - std::exp(-smoothSpeed * std::max(0.0f, deltaTime));
        const glm::vec3 smoothed = glm::mix(camera->GetFocalPoint(), targetFocal, alpha);
        camera->SetFocalPoint(smoothed);
    }
};

#endif // CAMERA_CONTROLLER_H
