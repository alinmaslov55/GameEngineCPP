#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

void DecomposeMatrix(const glm::mat4& transform, glm::vec3& outPos, glm::vec3& outRot, glm::vec3& outScale) {
    outPos = glm::vec3(transform[3]);
    outScale.x = glm::length(glm::vec3(transform[0]));
    outScale.y = glm::length(glm::vec3(transform[1]));
    outScale.z = glm::length(glm::vec3(transform[2]));

    glm::mat3 rotMat;
    rotMat[0] = (outScale.x > 1e-6f) ? (glm::vec3(transform[0]) / outScale.x) : glm::vec3(1.0f, 0.0f, 0.0f);
    rotMat[1] = (outScale.y > 1e-6f) ? (glm::vec3(transform[1]) / outScale.y) : glm::vec3(0.0f, 1.0f, 0.0f);
    rotMat[2] = (outScale.z > 1e-6f) ? (glm::vec3(transform[2]) / outScale.z) : glm::vec3(0.0f, 0.0f, 1.0f);

    float sy = rotMat[2][0];
    if (sy < 0.99999f) {
        if (sy > -0.99999f) {
            outRot.y = std::asin(sy);
            outRot.x = std::atan2(-rotMat[2][1], rotMat[2][2]);
            outRot.z = std::atan2(-rotMat[1][0], rotMat[0][0]);
        } else {
            outRot.y = -glm::half_pi<float>();
            outRot.x = -std::atan2(rotMat[0][1], rotMat[1][1]);
            outRot.z = 0.0f;
        }
    } else {
        outRot.y = glm::half_pi<float>();
        outRot.x = std::atan2(rotMat[0][1], rotMat[1][1]);
        outRot.z = 0.0f;
    }
    outRot = glm::degrees(outRot);
}

} // namespace

Transform::~Transform() {
    // Detach from parent
    if (m_Parent) {
        auto& siblings = m_Parent->m_Children;
        std::erase(siblings, this);
        m_Parent = nullptr;
    }
    // Orphan remaining children safely
    for (auto* child : m_Children) {
        if (child) {
            child->m_Parent = nullptr;
            child->SetDirty();
        }
    }
    m_Children.clear();
}

bool Transform::IsDescendantOf(const Transform* ancestor) const noexcept {
    if (!ancestor) return false;
    for (const Transform* curr = m_Parent; curr != nullptr; curr = curr->m_Parent) {
        if (curr == ancestor) return true;
    }
    return false;
}

void Transform::SetDirty() const noexcept {
    m_WorldDirty = true;
    for (auto* child : m_Children) {
        if (child && !child->m_WorldDirty) {
            child->SetDirty();
        }
    }
}

bool Transform::SetParent(Transform* newParent, bool keepWorldTransform) {
    if (newParent == m_Parent) {
        return true;
    }

    // Cycle rejection: cannot parent to self or descendant
    if (newParent == this || (newParent && newParent->IsDescendantOf(this))) {
        ENGINE_CORE_WARN("Transform::SetParent: Cycle detected! Cannot parent transform to itself or a descendant.");
        return false;
    }

    if (keepWorldTransform) {
        const glm::mat4 currentWorld = GetWorldMatrix();
        if (newParent) {
            const glm::mat4 newParentWorldInv = glm::inverse(newParent->GetWorldMatrix());
            const glm::mat4 newLocal = newParentWorldInv * currentWorld;
            DecomposeMatrix(newLocal, position, rotation, scale);
        } else {
            DecomposeMatrix(currentWorld, position, rotation, scale);
        }
    }

    // Detach from old parent
    if (m_Parent) {
        auto& siblings = m_Parent->m_Children;
        std::erase(siblings, this);
    }

    m_Parent = newParent;

    // Attach to new parent
    if (m_Parent) {
        m_Parent->m_Children.push_back(this);
    }

    m_LastPosition = position;
    m_LastRotation = rotation;
    m_LastScale = scale;
    SetDirty();
    return true;
}

glm::mat4 Transform::GetLocalMatrix() const noexcept {
    glm::mat4 rotMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), glm::vec3(0, 0, 1));

    return glm::translate(glm::mat4(1.0f), position) *
           rotMatrix *
           glm::scale(glm::mat4(1.0f), scale);
}

void Transform::UpdateDirtyFromAncestors() const noexcept {
    if (m_Parent) {
        m_Parent->UpdateDirtyFromAncestors();
    }
    if (position != m_LastPosition || rotation != m_LastRotation || scale != m_LastScale) {
        m_LastPosition = position;
        m_LastRotation = rotation;
        m_LastScale = scale;
        SetDirty();
    }
}

const glm::mat4& Transform::GetWorldMatrix() const noexcept {
    UpdateDirtyFromAncestors();

    if (m_WorldDirty) {
        const glm::mat4 localMatrix = GetLocalMatrix();
        if (m_Parent) {
            m_CachedWorldMatrix = m_Parent->GetWorldMatrix() * localMatrix;
        } else {
            m_CachedWorldMatrix = localMatrix;
        }
        m_WorldDirty = false;
    }
    return m_CachedWorldMatrix;
}

glm::vec3 Transform::GetWorldPosition() const noexcept {
    if (!m_Parent) {
        return position;
    }
    return glm::vec3(GetWorldMatrix()[3]);
}

void Transform::SetWorldPosition(const glm::vec3& worldPos) noexcept {
    if (!m_Parent) {
        position = worldPos;
    } else {
        const glm::mat4 parentWorldInv = glm::inverse(m_Parent->GetWorldMatrix());
        const glm::vec4 localPos = parentWorldInv * glm::vec4(worldPos, 1.0f);
        position = glm::vec3(localPos);
    }
    m_LastPosition = position;
    SetDirty();
}

glm::vec3 Transform::GetWorldRotation() const noexcept {
    if (!m_Parent) {
        return rotation;
    }
    glm::vec3 wPos, wRot, wScale;
    DecomposeMatrix(GetWorldMatrix(), wPos, wRot, wScale);
    return wRot;
}

glm::vec3 Transform::GetWorldScale() const noexcept {
    if (!m_Parent) {
        return scale;
    }
    glm::vec3 wPos, wRot, wScale;
    DecomposeMatrix(GetWorldMatrix(), wPos, wRot, wScale);
    return wScale;
}

glm::mat4 Transform::GetInterpolatedLocalMatrix(float alpha) const noexcept {
    glm::vec3 interpPos = GetInterpolatedPosition(alpha);
    glm::vec3 interpRot = GetInterpolatedRotation(alpha);
    glm::mat4 rotMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(interpRot.x), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(interpRot.y), glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(interpRot.z), glm::vec3(0, 0, 1));

    return glm::translate(glm::mat4(1.0f), interpPos) *
           rotMatrix *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 Transform::GetInterpolatedTransformMatrix(float alpha) const noexcept {
    if (!m_Parent) {
        return GetInterpolatedLocalMatrix(alpha);
    }
    return m_Parent->GetInterpolatedTransformMatrix(alpha) * GetInterpolatedLocalMatrix(alpha);
}

} // namespace Engine
