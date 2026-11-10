#ifndef SCENE_COMPONENT_H
#define SCENE_COMPONENT_H

#include <cstdint>

namespace Engine {
class GameObject;
class Scene;
struct Collision2DInfo;

class Component {
public:
    static constexpr uint16_t kInvalidTypeID = 0xFFFF;

    virtual ~Component() = default;

    virtual void Awake() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void FixedUpdate(float fixedDeltaTime) {}
    virtual void LateUpdate(float deltaTime) {}
    virtual void OnDestroy() {}

    // Object Pooling Lifecycle Hooks
    virtual void OnPoolAcquire() {}
    virtual void OnPoolRelease() {}

    // 2D Ground-plane physics & sensor callbacks
    virtual void OnCollisionEnter2D(const Collision2DInfo& /*info*/) {}
    virtual void OnCollisionExit2D(GameObject* /*other*/) {}
    virtual void OnTriggerEnter2D(GameObject* /*other*/) {}
    virtual void OnTriggerExit2D(GameObject* /*other*/) {}

    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept { return m_Enabled; }
    [[nodiscard]] bool HasStarted() const noexcept { return m_HasStarted; }
    [[nodiscard]] bool IsPendingRemoval() const noexcept { return m_PendingRemoval; }

    [[nodiscard]] uint16_t GetTypeID() const noexcept { return m_TypeID; }
    [[nodiscard]] uint16_t GetBaseTypeID() const noexcept { return m_BaseTypeID; }

    [[nodiscard]] GameObject* GetGameObject() const noexcept { return m_GameObject; }

protected:
    void SetStarted(bool started) noexcept { m_HasStarted = started; }

private:
    friend class GameObject;
    friend class Scene;

    GameObject* m_GameObject{ nullptr };
    uint16_t m_TypeID{ kInvalidTypeID };
    uint16_t m_BaseTypeID{ kInvalidTypeID };
    bool m_Enabled{ true };
    bool m_HasStarted{ false };
    bool m_PendingRemoval{ false };
};
} // namespace Engine

#endif // SCENE_COMPONENT_H
