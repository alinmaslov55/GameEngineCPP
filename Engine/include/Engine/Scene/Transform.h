#ifndef SCENE_TRANFORM_H
#define SCENE_TRANFORM_H

#include "Engine/Scene/Component.h"

#include <glm/glm.hpp>

namespace Engine {
class Transform : public Component {
public:
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation{ 0.0f, 0.0f, 0.0f }; // degrees or rads
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    Transform() = default;
    ~Transform() override = default;

    void Translate(const glm::vec3& translation) {
        position += translation;
    }

    void Rotate(const glm::vec3& eulerAngles) {
        rotation += eulerAngles;
    }
};
}

#endif // SCENE_TRANFORM_H
