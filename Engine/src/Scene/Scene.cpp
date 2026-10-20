#include "Engine/Scene/Scene.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>

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
    CameraComponent* mainCamera = nullptr;
    for (auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            mainCamera = cameraComp;
            break;
        }
    }

    if (!mainCamera) return;

    Renderer2D::BeginScene(mainCamera->Camera);

    for (auto& go : m_GameObjects) {
        auto* sprite = go->GetComponent<SpriteRenderer>();
        auto* transform = go->GetComponent<Transform>();

        if (sprite && transform) {
            glm::mat4 transformMatrix = glm::translate(glm::mat4(1.0f), transform->position) *
                                        glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.z), glm::vec3(0, 0, 1)) *
                                        glm::scale(glm::mat4(1.0f), transform->scale);

            Renderer2D::DrawQuad(transformMatrix, sprite->GetTexture(), sprite->GetColor());
        }
    }

    Renderer2D::EndScene();
}
} // namespace Engine
