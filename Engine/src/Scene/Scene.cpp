#include "Engine/Scene/Scene.h"
#include "Engine/Scene/CameraComponent.h"
#include "Engine/Renderer/SpriteRenderer.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Physics/PhysicsComponents.h"
#include "Engine/Scene/Tilemap.h"
#include "Engine/Scene/Transform.h"
#include "Engine/Math/Isometric.h"
#include "Engine/Utils/Logger.h"
#include "Engine/UI/UIPanel.h"
#include "Engine/UI/UILabel.h"
#include "Engine/Particles/ParticleSystem.h"

#include <box2d/box2d.h>
#include <box2d/math_functions.h>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Engine {
Scene::Scene(const std::string& name)
    : m_Name(name), m_Camera(-1.6f, 1.6f, -0.9f, 0.9f) {
    ENGINE_CORE_INFO("Scene created: '{}'", m_Name);
}

Scene::~Scene() {
    if (m_IsRuntimeRunning) {
        OnRuntimeStop();
    }
    ENGINE_CORE_INFO("Scene destroyed: '{}'", m_Name);
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}'", m_Name, name);
    auto gameObject = std::make_unique<GameObject>(name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

GameObject* Scene::CreateGameObjectWithUUID(uint64_t uuid, const std::string& name) {
    ENGINE_CORE_DEBUG("Scene '{}': Creating GameObject '{}' with UUID {}", m_Name, name, uuid);
    auto gameObject = std::make_unique<GameObject>(uuid, name);
    GameObject* rawPtr = gameObject.get();
    m_GameObjects.push_back(std::move(gameObject));
    return rawPtr;
}

void Scene::OnViewportResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }

    m_ViewportWidth = width;
    m_ViewportHeight = height;

    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    float orthoLeft   = -m_OrthographicSize * aspectRatio * 0.5f;
    float orthoRight  =  m_OrthographicSize * aspectRatio * 0.5f;
    float orthoBottom = -m_OrthographicSize * 0.5f;
    float orthoTop    =  m_OrthographicSize * 0.5f;
    m_Camera.SetProjection(orthoLeft, orthoRight, orthoBottom, orthoTop);

    for (auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            if (!cameraComp->FixedAspectRatio) {
                cameraComp->SetViewportSize(width, height);
            }
        }
    }

    ENGINE_CORE_DEBUG("Scene '{}': Viewport resized to {}x{} (aspect: {:.3f})", m_Name, width, height, aspectRatio);
}

const OrthographicCamera& Scene::GetActiveCamera() const {
    for (const auto& go : m_GameObjects) {
        if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
            return cameraComp->Camera;
        }
    }
    return m_Camera;
}

void Scene::SetOrthographicSize(float size) {
    if (size <= 0.0f) {
        return;
    }
    m_OrthographicSize = size;
    OnViewportResize(m_ViewportWidth, m_ViewportHeight);
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
    ENGINE_CORE_TRACE("Scene '{}': Update (deltaTime: {:.4f}s, {} GameObjects)", m_Name, deltaTime, m_GameObjects.size());
    for (auto& gameObject : m_GameObjects) {
        gameObject->Update(deltaTime);
    }

    std::vector<GameObject*> colliders;
    std::vector<GameObject*> tilemapObjects;
    for (auto& gameObject : m_GameObjects) {
        if (auto* collider = gameObject->GetComponent<BoxCollider2D>()) {
            colliders.push_back(gameObject.get());

            // Apply vertical isometric gravity & ground clamping for dynamic bodies
            if (collider->isDynamic && collider->useGravity) {
                auto* transform = gameObject->GetTransform();
                collider->verticalVelocity += collider->gravity * deltaTime;
                transform->position.y += collider->verticalVelocity * deltaTime;
                if (transform->position.y <= collider->groundLevel) {
                    transform->position.y = collider->groundLevel;
                    collider->verticalVelocity = 0.0f;
                }
            }
        }
        if (gameObject->GetComponent<Tilemap>()) {
            tilemapObjects.push_back(gameObject.get());
        }
    }

    // 1. Entity-vs-Entity AABB Collision on XZ Ground Plane
    for (size_t i = 0; i < colliders.size(); i++) {
        for (size_t j = i + 1; j < colliders.size(); j++) {
            GameObject* a = colliders[i];
            GameObject* b = colliders[j];

            auto cA = a->GetComponent<BoxCollider2D>();
            auto cB = b->GetComponent<BoxCollider2D>();

            if (!cA->isDynamic && !cB->isDynamic) continue;

            auto* tA = a->GetTransform();
            auto* tB = b->GetTransform();

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
                ENGINE_CORE_TRACE("Scene '{}': Collision resolved between '{}' and '{}' (overlap: {:.3f}, {:.3f})",
                                  m_Name, a->GetName(), b->GetName(), overlapX, overlapZ);
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

    // 2. Dynamic Entity-vs-Tilemap Non-Walkable Tile Collision on XZ Ground Plane
    for (GameObject* a : colliders) {
        auto* cA = a->GetComponent<BoxCollider2D>();
        if (!cA->isDynamic) continue;

        auto* tA = a->GetTransform();
        float halfW = (cA->size.x * tA->scale.x) * 0.5f;
        float halfD = (cA->size.y * tA->scale.z) * 0.5f;

        for (GameObject* mapObj : tilemapObjects) {
            auto* tilemap = mapObj->GetComponent<Tilemap>();
            auto* mapTransform = mapObj->GetTransform();
            if (!tilemap || tilemap->GetWidth() == 0 || tilemap->GetHeight() == 0) continue;

            float aLeft  = tA->position.x + cA->offset.x - halfW;
            float aRight = tA->position.x + cA->offset.x + halfW;
            float aBack  = tA->position.z + cA->offset.y - halfD;
            float aFront = tA->position.z + cA->offset.y + halfD;

            int minGridX = std::max(0, static_cast<int>(std::floor(aLeft - mapTransform->position.x + 0.5f)));
            int maxGridX = std::min(static_cast<int>(tilemap->GetWidth()) - 1,
                                    static_cast<int>(std::floor(aRight - mapTransform->position.x + 0.5f)));
            int minGridZ = std::max(0, static_cast<int>(std::floor(aBack - mapTransform->position.z + 0.5f)));
            int maxGridZ = std::min(static_cast<int>(tilemap->GetHeight()) - 1,
                                    static_cast<int>(std::floor(aFront - mapTransform->position.z + 0.5f)));

            for (int gz = minGridZ; gz <= maxGridZ; ++gz) {
                for (int gx = minGridX; gx <= maxGridX; ++gx) {
                    if (tilemap->GetTile(static_cast<uint32_t>(gx), static_cast<uint32_t>(gz)).walkable) {
                        continue;
                    }

                    // Recompute dynamic AABB in case a neighboring tile already adjusted tA->position
                    aLeft  = tA->position.x + cA->offset.x - halfW;
                    aRight = tA->position.x + cA->offset.x + halfW;
                    aBack  = tA->position.z + cA->offset.y - halfD;
                    aFront = tA->position.z + cA->offset.y + halfD;

                    float tileCenterX = mapTransform->position.x + static_cast<float>(gx);
                    float tileCenterZ = mapTransform->position.z + static_cast<float>(gz);
                    float tileHalfX   = 0.5f * mapTransform->scale.x;
                    float tileHalfZ   = 0.5f * mapTransform->scale.z;

                    float tLeft  = tileCenterX - tileHalfX;
                    float tRight = tileCenterX + tileHalfX;
                    float tBack  = tileCenterZ - tileHalfZ;
                    float tFront = tileCenterZ + tileHalfZ;

                    float overlapX = std::min(aRight, tRight) - std::max(aLeft, tLeft);
                    float overlapZ = std::min(aFront, tFront) - std::max(aBack, tBack);

                    if (overlapX > 0.0f && overlapZ > 0.0f) {
                        float centerAX = tA->position.x + cA->offset.x;
                        float centerAZ = tA->position.z + cA->offset.y;
                        if (overlapX < overlapZ) {
                            tA->position.x += (centerAX < tileCenterX) ? -overlapX : overlapX;
                        } else {
                            tA->position.z += (centerAZ < tileCenterZ) ? -overlapZ : overlapZ;
                        }
                    }
                }
            }
        }
    }
}

void Scene::OnRuntimeStart() {
    if (m_IsRuntimeRunning) {
        ENGINE_CORE_WARN("Scene '{}': OnRuntimeStart called while already running", m_Name);
        return;
    }

    ENGINE_CORE_INFO("Scene '{}': Starting Box2D runtime physics", m_Name);

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{ 0.0f, -9.8f };
    b2WorldId worldId = b2CreateWorld(&worldDef);
    memcpy(&m_PhysicsWorldId, &worldId, sizeof(worldId));
    m_IsRuntimeRunning = true;

    for (auto& go : m_GameObjects) {
        auto* transform = go->GetTransform();
        auto* rb2d = go->GetComponent<Rigidbody2D>();

        if (rb2d && transform) {
            b2BodyDef bodyDef = b2DefaultBodyDef();
            if (rb2d->Type == Rigidbody2D::BodyType::Static) {
                bodyDef.type = b2_staticBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Dynamic) {
                bodyDef.type = b2_dynamicBody;
            } else if (rb2d->Type == Rigidbody2D::BodyType::Kinematic) {
                bodyDef.type = b2_kinematicBody;
            }

            bodyDef.position = b2Vec2{ transform->position.x, transform->position.y };
            bodyDef.rotation = b2MakeRot(glm::radians(transform->rotation.z));
            bodyDef.fixedRotation = rb2d->FixedRotation;
            bodyDef.name = go->GetName().c_str();

            b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
            memset(&rb2d->RuntimeBody, 0, sizeof(void*));
            memcpy(&rb2d->RuntimeBody, &bodyId, sizeof(bodyId));

            if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = bc2d->density;
                shapeDef.material.friction = bc2d->friction;
                shapeDef.material.restitution = bc2d->restitution;

                b2Vec2 center = { bc2d->offset.x, bc2d->offset.y };
                b2Rot rot = b2MakeRot(0.0f);
                float halfW = std::max(0.001f, bc2d->size.x * std::abs(transform->scale.x) * 0.5f);
                float halfH = std::max(0.001f, bc2d->size.y * std::abs(transform->scale.y) * 0.5f);
                b2Polygon box = b2MakeOffsetBox(halfW, halfH, center, rot);

                b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &box);
                memset(&bc2d->RuntimeFixture, 0, sizeof(void*));
                memcpy(&bc2d->RuntimeFixture, &shapeId, sizeof(shapeId));
            }
        }
    }
}

void Scene::OnRuntimeStop() {
    if (!m_IsRuntimeRunning) {
        return;
    }

    ENGINE_CORE_INFO("Scene '{}': Stopping Box2D runtime physics", m_Name);

    b2WorldId worldId;
    memcpy(&worldId, &m_PhysicsWorldId, sizeof(worldId));
    if (b2World_IsValid(worldId)) {
        b2DestroyWorld(worldId);
    }
    m_PhysicsWorldId = 0;
    m_IsRuntimeRunning = false;

    for (auto& go : m_GameObjects) {
        if (auto* rb2d = go->GetComponent<Rigidbody2D>()) {
            rb2d->RuntimeBody = nullptr;
        }
        if (auto* bc2d = go->GetComponent<BoxCollider2D>()) {
            bc2d->RuntimeFixture = nullptr;
        }
    }
}

void Scene::UpdateRuntime(float deltaTime) {
    if (m_IsRuntimeRunning) {
        b2WorldId worldId;
        memcpy(&worldId, &m_PhysicsWorldId, sizeof(worldId));
        if (b2World_IsValid(worldId)) {
            b2World_Step(worldId, deltaTime, 4);
        }

        // Sync Box2D physics simulation results back to Engine Transform
        for (auto& go : m_GameObjects) {
            auto* transform = go->GetTransform();
            auto* rb2d = go->GetComponent<Rigidbody2D>();

            if (rb2d && transform && rb2d->RuntimeBody) {
                b2BodyId bodyId;
                memcpy(&bodyId, &rb2d->RuntimeBody, sizeof(bodyId));
                if (b2Body_IsValid(bodyId)) {
                    b2Vec2 pos = b2Body_GetPosition(bodyId);
                    b2Rot rot = b2Body_GetRotation(bodyId);
                    transform->position.x = pos.x;
                    transform->position.y = pos.y;
                    transform->rotation.z = glm::degrees(b2Rot_GetAngle(rot));
                }
            }
        }
    }

    Update(deltaTime);
}

void Scene::Render(const OrthographicCamera* overrideCamera) {
    ENGINE_CORE_TRACE("Scene '{}': Render", m_Name);

    if (overrideCamera) {
        Renderer2D::BeginScene(*overrideCamera);
    } else {
        CameraComponent* mainCamera = nullptr;
        Transform* cameraTransform = nullptr;

        for (auto& go : m_GameObjects) {
            if (auto* cameraComp = go->GetComponent<CameraComponent>()) {
                mainCamera = cameraComp;
                cameraTransform = go->GetTransform();
                break;
            }
        }

        if (mainCamera) {
            if (cameraTransform) {
                mainCamera->Camera.SetPosition(cameraTransform->position);
                mainCamera->Camera.SetRotation(cameraTransform->rotation.z);
            }
            Renderer2D::BeginScene(mainCamera->Camera);
        } else {
            Renderer2D::BeginScene(m_Camera);
        }
    }

    // 1. Render Tilemaps (Ground Layer) first
    for (auto& gameObject : m_GameObjects) {
        auto tilemap = gameObject->GetComponent<Tilemap>();
        if (tilemap) {
            auto* transform = gameObject->GetTransform();
            
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
        if (gameObject->GetComponent<SpriteRenderer>() && gameObject->GetTransform()) {
            renderQueue.push_back(gameObject.get());
        }
    }

    // 3. Sort the queue by isometric depth
    std::sort(renderQueue.begin(), renderQueue.end(), [](GameObject* a, GameObject* b) {
        auto* tA = a->GetTransform();
        auto* tB = b->GetTransform();

        // In isometric 3D, depth is primarily calculated by X + Z.
        // Higher (X + Z) means it is further back into the screen.
        float depthA = tA->position.x + tA->position.z;
        float depthB = tB->position.x + tB->position.z;

        if (std::abs(depthA - depthB) < 1e-5f) {
            // Tie-breaker: lower physical Y renders first (behind), higher physical Y renders later (in front)
            return tA->position.y < tB->position.y;
        }

        return depthA > depthB;
    });

    // 4. Draw the sorted queue
    for (GameObject* gameObject : renderQueue) {
        auto spriteRenderer = gameObject->GetComponent<SpriteRenderer>();
        auto* transform = gameObject->GetTransform();

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
    
    // Create an orthographic projection matching the active viewport size
    // (0,0) at bottom-left, (m_ViewportWidth, m_ViewportHeight) at top-right
    glm::mat4 uiProjection = glm::ortho(
        0.0f, static_cast<float>(m_ViewportWidth),
        0.0f, static_cast<float>(m_ViewportHeight),
        -1.0f, 1.0f
    );
    
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
