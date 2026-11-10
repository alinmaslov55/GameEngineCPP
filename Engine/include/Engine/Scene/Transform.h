#ifndef SCENE_TRANSFORM_H
#define SCENE_TRANSFORM_H

#include "Engine/Scene/Component.h"
#include "Engine/Utils/Logger.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace Engine {

class Transform final : public Component {
public:
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 prevPosition{ 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation{ 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
    glm::vec3 prevRotation{ 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    Transform() = default;
    ~Transform() override;

    void SnapshotPrevious() noexcept {
        prevPosition = position;
        prevRotation = rotation;
    }

    [[nodiscard]] glm::vec3 GetInterpolatedPosition(float alpha) const noexcept {
        return glm::mix(prevPosition, position, glm::clamp(alpha, 0.0f, 1.0f));
    }

    [[nodiscard]] glm::vec3 GetInterpolatedRotation(float alpha) const noexcept {
        return glm::mix(prevRotation, rotation, glm::clamp(alpha, 0.0f, 1.0f));
    }

    void Translate(const glm::vec3& translation) {
        position += translation;
        SetDirty();
        ENGINE_CORE_TRACE("Transform::Translate by ({:.4f}, {:.4f}, {:.4f}) -> position: ({:.4f}, {:.4f}, {:.4f})",
                          translation.x, translation.y, translation.z, position.x, position.y, position.z);
    }

    void Rotate(const glm::vec3& eulerAngles) {
        rotation += eulerAngles;
        SetDirty();
        ENGINE_CORE_TRACE("Transform::Rotate by ({:.4f}, {:.4f}, {:.4f}) -> rotation: ({:.4f}, {:.4f}, {:.4f})",
                          eulerAngles.x, eulerAngles.y, eulerAngles.z, rotation.x, rotation.y, rotation.z);
    }

    // Hierarchy management
    bool SetParent(Transform* newParent, bool keepWorldTransform = true);
    [[nodiscard]] Transform* GetParent() const noexcept { return m_Parent; }
    [[nodiscard]] const std::vector<Transform*>& GetChildren() const noexcept { return m_Children; }
    [[nodiscard]] bool IsDescendantOf(const Transform* ancestor) const noexcept;

    // Matrices and world coordinate queries
    [[nodiscard]] glm::mat4 GetLocalMatrix() const noexcept;
    [[nodiscard]] glm::mat4 GetTransformMatrix() const noexcept { return GetWorldMatrix(); }
    [[nodiscard]] const glm::mat4& GetWorldMatrix() const noexcept;

    [[nodiscard]] glm::vec3 GetWorldPosition() const noexcept;
    void SetWorldPosition(const glm::vec3& worldPos) noexcept;
    [[nodiscard]] glm::vec3 GetWorldRotation() const noexcept;
    void SetWorldRotation(const glm::vec3& worldRot) noexcept;
    [[nodiscard]] glm::vec3 GetWorldScale() const noexcept;

    [[nodiscard]] glm::mat4 GetInterpolatedLocalMatrix(float alpha) const noexcept;
    [[nodiscard]] glm::mat4 GetInterpolatedTransformMatrix(float alpha) const noexcept;
    [[nodiscard]] glm::vec3 GetInterpolatedWorldPosition(float alpha) const noexcept;
    [[nodiscard]] glm::vec3 GetInterpolatedWorldRotation(float alpha) const noexcept;

    void SetDirty() const noexcept;

private:
    void UpdateDirtyFromAncestors() const noexcept;

    Transform* m_Parent = nullptr;
    std::vector<Transform*> m_Children;

    mutable bool m_WorldDirty = true;
    mutable glm::mat4 m_CachedWorldMatrix{ 1.0f };
    mutable glm::vec3 m_LastPosition{ 0.0f };
    mutable glm::vec3 m_LastRotation{ 0.0f };
    mutable glm::vec3 m_LastScale{ 1.0f };
};

} // namespace Engine

#endif // SCENE_TRANSFORM_H
