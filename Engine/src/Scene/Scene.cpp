#include "Engine/Scene/Scene.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Scene/Transform.h"
#include <algorithm>

namespace Engine {
Scene::Scene(const std::string& name)
    : m_Name(name), m_Camera(-1.6f, 1.6f, -0.9f, 0.9f) {}

GameObject* Scene::CreateGameObject(const std::string& name) {
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

void Scene::DestroyGameObject(GameObject* gameObject) {
    m_GameObjects.erase(
        std::remove_if(m_GameObjects.begin(), m_GameObjects.end(),
            [gameObject](const std::unique_ptr<GameObject>& ptr) {
                return ptr.get() == gameObject;
            }),
        m_GameObjects.end()
    );
}

void Scene::Update(float deltaTime) {
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }
}

void Scene::Render() {
    Renderer2D::BeginScene(m_Camera);

    for (auto& gameObject : m_GameObjects) {
        auto* transform = gameObject->GetComponent<Transform>();
        auto* sprite = gameObject->GetComponent<SpriteRenderer>();

        if (transform && sprite) {
            Renderer2D::DrawQuad(transform->GetTransformMatrix(), sprite->GetTexture(), sprite->GetColor());
        }
    }

    Renderer2D::EndScene();
}
} // namespace Engine
