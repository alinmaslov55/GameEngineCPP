#include "Engine/Scene/Scene.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Scene/BoxCollider2D.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Math/Isometric.h"
#include "Engine/Utils/Logger.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Particles/ParticleSystem.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>
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
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }

    std::vector<GameObject*> colliders;
    for (auto& gameObject : m_GameObjects) {
        if (gameObject->GetComponent<BoxCollider2D>()) {
            colliders.push_back(gameObject.get());
        }
    }

    for (size_t i = 0; i < colliders.size(); i++) {
        for (size_t j = i + 1; j < colliders.size(); j++) {
            GameObject* a = colliders[i];
            GameObject* b = colliders[j];

            auto cA = a->GetComponent<BoxCollider2D>();
            auto cB = b->GetComponent<BoxCollider2D>();

            if (!cA->isDynamic && !cB->isDynamic) continue;

            auto tA = a->GetComponent<Transform>();
            auto tB = b->GetComponent<Transform>();

            // Calculate AABB bounds on the XZ ground plane for Object A
            float aLeft  = tA->position.x + cA->offset.x - (cA->size.x * tA->scale.x) / 2.0f;
            float aRight = tA->position.x + cA->offset.x + (cA->size.x * tA->scale.x) / 2.0f;
            float aBack  = tA->position.z + cA->offset.y - (cA->size.y * tA->scale.z) / 2.0f;
            float aFront = tA->position.z + cA->offset.y + (cA->size.y * tA->scale.z) / 2.0f;

            // Calculate AABB bounds on the XZ ground plane for Object B
            float bLeft  = tB->position.x + cB->offset.x - (cB->size.x * tB->scale.x) / 2.0f;
            float bRight = tB->position.x + cB->offset.x + (cB->size.x * tB->scale.x) / 2.0f;
            float bBack  = tB->position.z + cB->offset.y - (cB->size.y * tB->scale.z) / 2.0f;
            float bFront = tB->position.z + cB->offset.y + (cB->size.y * tB->scale.z) / 2.0f;

            float overlapX = std::min(aRight, bRight) - std::max(aLeft, bLeft);
            float overlapZ = std::min(aFront, bFront) - std::max(aBack, bBack);

            if (overlapX > 0 && overlapZ > 0) {
                glm::vec2 push(0.0f);

                if (overlapX < overlapZ) {
                    push.x = (tA->position.x < tB->position.x) ? -overlapX : overlapX;
                } else {
                    push.y = (tA->position.z < tB->position.z) ? -overlapZ : overlapZ;
                }

                if (cA->isDynamic && !cB->isDynamic) {
                    tA->position.x += push.x;
                    tA->position.z += push.y;
                } else if (!cA->isDynamic && cB->isDynamic) {
                    tB->position.x -= push.x;
                    tB->position.z -= push.y;
                } else {
                    tA->position.x += push.x * 0.5f;
                    tA->position.z += push.y * 0.5f;
                    tB->position.x -= push.x * 0.5f;
                    tB->position.z -= push.y * 0.5f;
                }
            }
        }
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

    // 1. Render Tilemaps (Ground Layer) first
    for (auto& gameObject : m_GameObjects) {
        auto tilemap = gameObject->GetComponent<Tilemap>();
        if (tilemap) {
            auto transform = gameObject->GetComponent<Transform>();
            
            for (uint32_t y = 0; y < tilemap->GetHeight(); y++) {
                for (uint32_t x = 0; x < tilemap->GetWidth(); x++) {
                    uint32_t tileId = tilemap->GetTile(x, y).id;
                    if (tileId == 0) continue; // Skip empty tiles

                    auto subTexture = tilemap->tileSet->GetTileTexture(tileId);
                    if (!subTexture) continue;

                    // Grid coordinates act as X and Z in the 3D world
                    glm::vec3 worldPos = transform->position + glm::vec3(x, 0.0f, y);
                    
                    // Project to 2D Isometric Space
                    glm::vec2 screenPos = Isometric::WorldToScreen(worldPos);
                    glm::vec3 renderPos = { screenPos.x, screenPos.y, 0.0f };

                    glm::mat4 transformMatrix = glm::translate(glm::mat4(1.0f), renderPos) *
                                                glm::scale(glm::mat4(1.0f), transform->scale);

                    Renderer2D::DrawQuad(transformMatrix, subTexture);
                }
            }
        }
    }

    // 2. Collect all GameObjects that have a SpriteRenderer
    std::vector<GameObject*> renderQueue;
    for (auto& gameObject : m_GameObjects) {
        if (gameObject->GetComponent<SpriteRenderer>() && gameObject->GetComponent<Transform>()) {
            renderQueue.push_back(gameObject.get());
        }
    }

    // 3. Sort the queue by isometric depth
    std::sort(renderQueue.begin(), renderQueue.end(), [](GameObject* a, GameObject* b) {
        auto tA = a->GetComponent<Transform>();
        auto tB = b->GetComponent<Transform>();

        // In isometric 3D, depth is primarily calculated by X + Z.
        // Higher (X + Z) means it is further back into the screen.
        float depthA = tA->position.x + tA->position.z;
        float depthB = tB->position.x + tB->position.z;

        if (depthA == depthB) {
            // Tie-breaker: object with higher physical Y (height) renders in front (later)
            return tA->position.y > tB->position.y;
        }

        return depthA > depthB;
    });

    // 4. Draw the sorted queue
    for (GameObject* gameObject : renderQueue) {
        auto spriteRenderer = gameObject->GetComponent<SpriteRenderer>();
        auto transform = gameObject->GetComponent<Transform>();

        // Convert 3D world position to 2D isometric screen position ---
        glm::vec2 screenPos = Isometric::WorldToScreen(transform->position);
        glm::vec3 renderPos = { screenPos.x, screenPos.y, 0.0f }; // Z is 0 for 2D rendering

        // Use renderPos instead of transform->position
        glm::mat4 transformMatrix = glm::translate(glm::mat4(1.0f), renderPos) *
                                    glm::rotate(glm::mat4(1.0f), glm::radians(transform->rotation.z), glm::vec3(0, 0, 1)) *
                                    glm::scale(glm::mat4(1.0f), transform->scale);

        if (spriteRenderer->GetSubTexture()) {
            Renderer2D::DrawQuad(transformMatrix, spriteRenderer->GetSubTexture(), spriteRenderer->GetColor());
        } else {
            Renderer2D::DrawQuad(transformMatrix, spriteRenderer->GetTexture(), spriteRenderer->GetColor());
        }
    }

    // 2.5 Render Particles (in 3D Isometric Space)
    for (auto& gameObject : m_GameObjects) {
        auto particleSystem = gameObject->GetComponent<ParticleSystem>();
        if (particleSystem) {
            for (const auto& particle : particleSystem->GetParticles()) {
                if (!particle.active) continue;

                // Calculate lifecycle ratio (0.0 = dead, 1.0 = just spawned)
                float lifeRatio = particle.lifeRemaining / particle.lifeTime;
                
                // Interpolate size and color over time
                float size = glm::mix(particle.sizeEnd, particle.sizeBegin, lifeRatio);
                glm::vec4 color = glm::mix(particle.colorEnd, particle.colorBegin, lifeRatio);

                // Convert 3D world position to 2D isometric screen space
                glm::vec2 screenPos = Isometric::WorldToScreen(particle.position);
                glm::vec3 renderPos = { screenPos.x, screenPos.y, 0.0f };

                glm::mat4 transform = glm::translate(glm::mat4(1.0f), renderPos) *
                                      glm::rotate(glm::mat4(1.0f), particle.rotation, glm::vec3(0.0f, 0.0f, 1.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(size));

                // Draw the particle as a flat colored quad (or you can bind a subtexture here)
                Renderer2D::DrawQuad(transform, color);
            }
        }
    }

    Renderer2D::EndScene();

    // ==========================================
    // 3. UI RENDERING PASS (Screen Space)
    // ==========================================
    
    // Create an orthographic projection matching your window size (e.g., 1280x720)
    // (0,0) at bottom-left, (1280, 720) at top-right
    glm::mat4 uiProjection = glm::ortho(0.0f, 1280.0f, 0.0f, 720.0f, -1.0f, 1.0f);
    
    Renderer2D::BeginScene(uiProjection); 

    for (auto& gameObject : m_GameObjects) {
        for (auto* uiPanel : gameObject->GetComponents<UIPanel>()) {
            // UI elements don't use the standard Transform component.
            // We construct a simple model matrix using the screen-space pixel coordinates.
            glm::mat4 uiTransform = glm::translate(glm::mat4(1.0f), glm::vec3(uiPanel->position, 0.0f)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(uiPanel->size, 1.0f));

            if (uiPanel->texture) {
                Renderer2D::DrawQuad(uiTransform, uiPanel->texture, uiPanel->color);
            } else {
                Renderer2D::DrawQuad(uiTransform, uiPanel->color);
            }
        }
    }

    for (auto& gameObject : m_GameObjects) {
        for (auto* uiLabel : gameObject->GetComponents<UILabel>()) {
            if (uiLabel->font) {
                // Track the baseline starting position
                float cursorX = uiLabel->position.x;
                float cursorY = uiLabel->position.y;

                for (char c : uiLabel->text) {
                    const Character& ch = uiLabel->font->GetCharacter(c);

                    // Calculate the exact screen position of this specific character quad
                    float xPos = cursorX + (ch.bearing.x * uiLabel->scale);
                    
                    // Subtract (size.y - bearing.y) to align characters with hanging tails (like 'g' or 'p')
                    float yPos = cursorY - ((ch.size.y - ch.bearing.y) * uiLabel->scale);

                    glm::vec2 currentSize = ch.size * uiLabel->scale;

                    // Build the transform matrix
                    glm::mat4 charTransform = glm::translate(glm::mat4(1.0f), glm::vec3(xPos, yPos, 0.0f)) *
                                              glm::scale(glm::mat4(1.0f), glm::vec3(currentSize, 1.0f));

                    // Draw the character
                    Renderer2D::DrawQuad(charTransform, ch.subTexture, uiLabel->color);

                    // Advance the cursor for the next character
                    cursorX += (ch.advance * uiLabel->scale);
                }
            }
        }
    }

    Renderer2D::EndScene();
}
} // namespace Engine
