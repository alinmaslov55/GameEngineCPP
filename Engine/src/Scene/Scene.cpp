#include "Engine/Scene/Scene.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>

namespace Engine {
Scene::Scene(const std::string& name)
    : m_Name(name), m_Camera(-1.6f, 1.6f, -0.9f, 0.9f) {
    ENGINE_CORE_INFO("Scene created: '{}'", m_Name);
}

Scene::~Scene() {
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}'", m_Name, name);
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

void Scene::DestroyGameObject(GameObject* gameObject) {
    if (!gameObject) {
        ENGINE_CORE_WARN("Scene '{}': Attempted to destroy null GameObject", m_Name);
        return;
    }
    ENGINE_CORE_DEBUG("Scene '{}': Destroying GameObject '{}'", m_Name, gameObject->GetName());
    m_GameObjects.erase(
        std::remove_if(m_GameObjects.begin(), m_GameObjects.end(),
            [gameObject](const std::unique_ptr<GameObject>& ptr) {
                return ptr.get() == gameObject;
            }),
        m_GameObjects.end()
    );
}

void Scene::Update(float deltaTime) {
    ENGINE_CORE_TRACE("Scene '{}': Update ({} objects, dt: {:.4f}s)", m_Name, m_GameObjects.size(), deltaTime);
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }
}

void Scene::Render() {
    ENGINE_CORE_TRACE("Scene '{}': Render", m_Name);
    CameraComponent* mainCamera = nullptr;
    Transform* cameraTransform = nullptr;

    for (auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            mainCamera = cameraComp;
            cameraTransform = go->GetComponent<Transform>();
            break;
        }
    }

    if (mainCamera) {
        if (cameraTransform) {
            // TODO: Avoid redundant view matrix recalculation every frame if camera transform hasn't changed
            mainCamera->Camera.SetPosition(cameraTransform->position);
            mainCamera->Camera.SetRotation(cameraTransform->rotation.z);
        }
        Renderer2D::BeginScene(mainCamera->Camera);
    } else {
        Renderer2D::BeginScene(m_Camera);
    }

    // 1. Collect all GameObjects that have a SpriteRenderer
    std::vector<GameObject*> renderQueue;
    for (auto& gameObject : m_GameObjects) {
        if (gameObject->GetComponent<SpriteRenderer>() && gameObject->GetComponent<Transform>()) {
            renderQueue.push_back(gameObject.get());
        }
    }

    // 2. Sort the queue by Y-position (Descending)
    std::sort(renderQueue.begin(), renderQueue.end(), [](GameObject* a, GameObject* b) {
        auto transformA = a->GetComponent<Transform>();
        auto transformB = b->GetComponent<Transform>();

        // Higher Y means further back, so it should render first.
        // If Y is exactly the same, we fallback to X to prevent Z-fighting flickering
        if (transformA->position.y == transformB->position.y) {
            return transformA->position.x < transformB->position.x;
        }
        return transformA->position.y > transformB->position.y;
    });

    // 3. Draw the sorted queue
    for (GameObject* gameObject : renderQueue) {
        auto spriteRenderer = gameObject->GetComponent<SpriteRenderer>();
        auto transform = gameObject->GetComponent<Transform>();

        glm::mat4 transformMatrix = glm::translate(glm::mat4(1.0f), transform->position) *
                                    glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.z), glm::vec3(0, 0, 1)) *
                                    glm::scale(glm::mat4(1.0f), transform->scale);

        if (spriteRenderer->GetSubTexture()) {
            Renderer2D::DrawQuad(transformMatrix, spriteRenderer->GetSubTexture(), spriteRenderer->GetColor());
        } else {
            Renderer2D::DrawQuad(transformMatrix, spriteRenderer->GetTexture(), spriteRenderer->GetColor());
        }
    }

    Renderer2D::EndScene();
}
} // namespace Engine
