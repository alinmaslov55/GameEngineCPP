#include "Engine/Scene/Transform.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Utils/Logger.h"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

glm::mat4 EulerDegreesToRotationMatrix(const glm::vec3& eulerDeg) noexcept {
    return glm::rotate(glm::mat4(1.0f), glm::radians(eulerDeg.x), glm::vec3(1, 0, 0)) *
           glm::rotate(glm::mat4(1.0f), glm::radians(eulerDeg.y), glm::vec3(0, 1, 0)) *
           glm::rotate(glm::mat4(1.0f), glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
}

glm::vec3 DecomposeRotationMatrix(const glm::mat3& rotMat) noexcept {
    glm::vec3 outRot(0.0f);
    const float sy = std::clamp(rotMat[2][0], -1.0f, 1.0f);
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
    return glm::degrees(outRot);
}

glm::mat3 ExtractOrthonormalRotation(const glm::mat4& transform, glm::vec3& outScale) noexcept {
    outScale.x = glm::length(glm::vec3(transform[0]));
    outScale.y = glm::length(glm::vec3(transform[1]));
    outScale.z = glm::length(glm::vec3(transform[2]));

    glm::mat3 rotMat;
    rotMat[0] = (outScale.x > 1e-6f) ? (glm::vec3(transform[0]) / outScale.x) : glm::vec3(1.0f, 0.0f, 0.0f);
    rotMat[1] = (outScale.y > 1e-6f) ? (glm::vec3(transform[1]) / outScale.y) : glm::vec3(0.0f, 1.0f, 0.0f);
    rotMat[2] = (outScale.z > 1e-6f) ? (glm::vec3(transform[2]) / outScale.z) : glm::vec3(0.0f, 0.0f, 1.0f);
    return rotMat;
}

void DecomposeMatrix(const glm::mat4& transform, glm::vec3& outPos, glm::vec3& outRot, glm::vec3& outScale) noexcept {
    outPos = glm::vec3(transform[3]);
    const glm::mat3 rotMat = ExtractOrthonormalRotation(transform, outScale);
    outRot = DecomposeRotationMatrix(rotMat);
}

class ScopedAtomicSpinLock {
public:
    explicit ScopedAtomicSpinLock(std::atomic_flag& flag) noexcept
        : m_Flag(flag) {
        while (m_Flag.test_and_set(std::memory_order_acquire)) {
        }
    }

    ~ScopedAtomicSpinLock() noexcept {
        m_Flag.clear(std::memory_order_release);
    }

    ScopedAtomicSpinLock(const ScopedAtomicSpinLock&) = delete;
    ScopedAtomicSpinLock& operator=(const ScopedAtomicSpinLock&) = delete;

private:
    std::atomic_flag& m_Flag;
};

} // namespace

Transform::~Transform() {
    // Detach from parent
    if (m_Parent) {
        auto& siblings = m_Parent->m_Children;
        std::erase(siblings, this);
        m_Parent = nullptr;
    }
    // Orphan remaining children safely
    if (!m_Children.empty()) {
        ENGINE_CORE_TRACE("Transform::~Transform orphaning {} child transforms", m_Children.size());
    }
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
    m_WorldDirty.store(true, std::memory_order_release);
    for (auto* child : m_Children) {
        if (child && !child->m_WorldDirty.load(std::memory_order_acquire)) {
            child->SetDirty();
        }
    }
}

void Transform::NotifySpatialTransformChanged() const noexcept {
    if (auto* go = GetGameObject()) {
        go->NotifyStaticSpatialColliderStateChanged();
    }
    for (const auto* child : m_Children) {
        if (child) {
            child->NotifySpatialTransformChanged();
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

    const bool wasActiveInHier = GetGameObject() ? GetGameObject()->IsActiveInHierarchy() : false;

    if (keepWorldTransform) {
        const glm::mat4 currentWorld = GetWorldMatrix();
        if (newParent) {
            const glm::mat4 newParentWorldInv = SafeInvertMatrix(newParent->GetWorldMatrix());
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
    NotifySpatialTransformChanged();

    const bool isActiveInHier = GetGameObject() ? GetGameObject()->IsActiveInHierarchy() : false;
    if (wasActiveInHier != isActiveInHier) {
        if (auto* go = GetGameObject()) {
            go->NotifyHierarchyActiveChanged();
        }
    }

    ENGINE_CORE_DEBUG("Transform::SetParent: '{}' reparented to '{}' (keepWorld={})",
                      GetGameObject() ? GetGameObject()->GetName() : "<unnamed>",
                      (m_Parent && m_Parent->GetGameObject()) ? m_Parent->GetGameObject()->GetName() : "<root>",
                      keepWorldTransform);
    return true;
}

glm::mat4 Transform::SafeInvertMatrix(const glm::mat4& mat) noexcept {
    const float det = glm::determinant(mat);
    if (std::abs(det) >= 1e-6f) {
        return glm::inverse(mat);
    }

    // Singular matrix (zero or near-zero scale along one or more axes).
    // Construct regularized pseudo-inverse from TRS decomposition to prevent NaNs/Infs.
    glm::vec3 scale(1.0f);
    const glm::mat3 R = ExtractOrthonormalRotation(mat, scale);
    const glm::vec3 T = glm::vec3(mat[3]);

    glm::vec3 invScale(0.0f);
    if (std::abs(scale.x) >= 1e-6f) invScale.x = 1.0f / scale.x;
    if (std::abs(scale.y) >= 1e-6f) invScale.y = 1.0f / scale.y;
    if (std::abs(scale.z) >= 1e-6f) invScale.z = 1.0f / scale.z;

    // S^+ * R^T
    const glm::mat3 invRotScale = glm::mat3(glm::scale(glm::mat4(1.0f), invScale)) * glm::transpose(R);
    const glm::vec3 invT = -invRotScale * T;

    glm::mat4 result(invRotScale);
    result[3] = glm::vec4(invT, 1.0f);
    return result;
}

glm::mat4 Transform::GetLocalMatrix() const noexcept {
    return glm::translate(glm::mat4(1.0f), position) *
           EulerDegreesToRotationMatrix(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 Transform::GetWorldMatrix() const noexcept {
    // Evaluate parent BEFORE acquiring m_CacheLock so locks never nest along the hierarchy
    // and any untracked ancestor edits propagate SetDirty() before this node checks its flag.
    const glm::mat4 parentWorld = m_Parent ? m_Parent->GetWorldMatrix() : glm::mat4(1.0f);

    ScopedAtomicSpinLock lock(m_CacheLock);
    if (position != m_LastPosition || rotation != m_LastRotation || scale != m_LastScale) {
        m_LastPosition = position;
        m_LastRotation = rotation;
        m_LastScale = scale;
        SetDirty();
    }

    if (m_WorldDirty.load(std::memory_order_acquire)) {
        const glm::mat4 localMatrix = GetLocalMatrix();
        m_CachedWorldMatrix = m_Parent ? (parentWorld * localMatrix) : localMatrix;
        m_WorldDirty.store(false, std::memory_order_release);
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
        const glm::mat4 parentWorldInv = SafeInvertMatrix(m_Parent->GetWorldMatrix());
        const glm::vec4 localPos = parentWorldInv * glm::vec4(worldPos, 1.0f);
        position = glm::vec3(localPos);
    }
    m_LastPosition = position;
    SetDirty();
    NotifySpatialTransformChanged();
}

glm::vec3 Transform::GetWorldRotation() const noexcept {
    if (!m_Parent) {
        return rotation;
    }
    glm::vec3 wPos, wRot, wScale;
    DecomposeMatrix(GetWorldMatrix(), wPos, wRot, wScale);
    return wRot;
}

void Transform::SetWorldRotation(const glm::vec3& worldRot) noexcept {
    if (!m_Parent) {
        rotation = worldRot;
    } else {
        glm::vec3 unusedParentScale(1.0f);
        const glm::mat3 parentRotMat = ExtractOrthonormalRotation(m_Parent->GetWorldMatrix(), unusedParentScale);
        const glm::mat3 desiredWorldRotMat(EulerDegreesToRotationMatrix(worldRot));
        const glm::mat3 localRotMat = glm::transpose(parentRotMat) * desiredWorldRotMat;
        rotation = DecomposeRotationMatrix(localRotMat);
    }
    m_LastRotation = rotation;
    SetDirty();
    NotifySpatialTransformChanged();
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
    const glm::vec3 interpPos = GetInterpolatedPosition(alpha);
    const glm::vec3 interpRot = GetInterpolatedRotation(alpha);

    return glm::translate(glm::mat4(1.0f), interpPos) *
           EulerDegreesToRotationMatrix(interpRot) *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 Transform::GetInterpolatedTransformMatrix(float alpha) const noexcept {
    if (!m_Parent) {
        return GetInterpolatedLocalMatrix(alpha);
    }
    return m_Parent->GetInterpolatedTransformMatrix(alpha) * GetInterpolatedLocalMatrix(alpha);
}

glm::vec3 Transform::GetInterpolatedWorldPosition(float alpha) const noexcept {
    if (!m_Parent) {
        return GetInterpolatedPosition(alpha);
    }
    const glm::mat4 worldMat = GetInterpolatedTransformMatrix(alpha);
    return glm::vec3(worldMat[3]);
}

glm::vec3 Transform::GetInterpolatedWorldRotation(float alpha) const noexcept {
    if (!m_Parent) {
        return GetInterpolatedRotation(alpha);
    }
    const glm::mat4 worldMat = GetInterpolatedTransformMatrix(alpha);
    glm::vec3 wPos, wRot, wScale;
    DecomposeMatrix(worldMat, wPos, wRot, wScale);
    return wRot;
}

} // namespace Engine
