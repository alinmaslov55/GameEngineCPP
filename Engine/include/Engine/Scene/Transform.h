#ifndef SCENE_TRANFORM_H
#define SCENE_TRANFORM_H

#include "Engine/Scene/Component.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

    glm::mat4 GetTransformMatrix() const {
        glm::mat4 rotMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), glm::vec3(1, 0, 0)) *
                              glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), glm::vec3(0, 1, 0)) *
                              glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), glm::vec3(0, 0, 1));

        return glm::translate(glm::mat4(1.0f), position) *
               rotMatrix *
               glm::scale(glm::mat4(1.0f), scale);
    }
};
}

#endif // SCENE_TRANFORM_H
