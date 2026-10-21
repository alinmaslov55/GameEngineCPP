#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Transform.h>
#include <Engine/Renderer/OrthographicCamera.h>
#include <Engine/Math/Isometric.h>
#include <glm/glm.hpp>
#include <algorithm>

class CameraController : public Engine::Component {
public:
    Engine::OrthographicCamera* camera = nullptr;

    glm::vec2 deadzone = { 1.5f, 0.8f }; 

    void Update(float deltaTime) override {
        if (!camera) return;

        auto transform = GetGameObject()->GetComponent<Engine::Transform>();
        if (!transform) return;

        glm::vec3 camPos = camera->GetPosition();
        glm::vec2 playerScreenPos = Engine::Isometric::WorldToScreen(transform->position);

        float dx = playerScreenPos.x - camPos.x;
        float dy = playerScreenPos.y - camPos.y;

        if (dx > deadzone.x / 2.0f) {
            camPos.x = playerScreenPos.x - (deadzone.x / 2.0f);
        } else if (dx < -deadzone.x / 2.0f) {
            camPos.x = playerScreenPos.x + (deadzone.x / 2.0f);
        }

        if (dy > deadzone.y / 2.0f) {
            camPos.y = playerScreenPos.y - (deadzone.y / 2.0f);
        } else if (dy < -deadzone.y / 2.0f) {
            camPos.y = playerScreenPos.y + (deadzone.y / 2.0f);
        }

        camera->SetPosition(camPos);
    }
};

#endif // CAMERA_CONTROLLER_H