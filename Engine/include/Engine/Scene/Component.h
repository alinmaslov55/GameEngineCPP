#ifndef SCENE_COMPONENT_H
#define SCENE_COMPONENT_H

namespace Engine {
class GameObject;
class Scene;

class Component {
public:
    virtual ~Component() = default;

    virtual void Awake() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void FixedUpdate(float fixedDeltaTime) {}
    virtual void LateUpdate(float deltaTime) {}
    virtual void OnDestroy() {}

    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept { return m_Enabled; }
    [[nodiscard]] bool HasStarted() const noexcept { return m_HasStarted; }

    [[nodiscard]] GameObject* GetGameObject() const noexcept { return m_GameObject; }

protected:
    void SetStarted(bool started) noexcept { m_HasStarted = started; }

private:
    friend class GameObject;
    friend class Scene;

    GameObject* m_GameObject{ nullptr };
    bool m_Enabled{ true };
    bool m_HasStarted{ false };
};
} // namespace Engine

#endif // SCENE_COMPONENT_H
