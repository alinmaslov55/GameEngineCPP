#ifndef SCENE_COMPONENT_H
#define SCENE_COMPONENT_H

namespace Engine {
class GameObject;

class Component {
public:
    virtual ~Component() = default;

    virtual void Awake() {}
    virtual void OnEnable() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void LateUpdate(float deltaTime) {}
    virtual void OnDestroy() {}

    GameObject* GetGameObject() const { return m_GameObject; }

private:
    friend class GameObject;
    GameObject* m_GameObject{nullptr};
};
}

#endif // SCENE_COMPONENT_H
