#ifndef SANDBOX_STRUCTURE_PLACER_COMPONENT_H
#define SANDBOX_STRUCTURE_PLACER_COMPONENT_H

#include "Scripts/DeployableStructures.h"
#include "Scripts/PlayerController.h"
#include "Scripts/PlayerStatsComponent.h"

#include <Engine/Core/Input.h>
#include <Engine/Core/ResourceManager.h>
#include <Engine/Data/GameplayData.h>
#include <Engine/Data/GameplayDefinitions.h>
#include <Engine/Physics/PhysicsLayer.h>
#include <Engine/Physics/PhysicsQueries.h>
#include <Engine/Renderer/SpriteRenderer.h>
#include <Engine/Scene/Component.h>
#include <Engine/Scene/GameObject.h>
#include <Engine/Scene/Prefab.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/Transform.h>
#include <Engine/UI/UIRadialWheelComponent.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

class StructurePlacerComponent final : public Engine::Component {
public:
    float PlacementDistance = 2.2f;
    float ClearanceRadius = 0.55f;

    void Awake() override {
        Engine::Input::BindAction("DropWheel", {
            .PrimaryKey = SDL_SCANCODE_TAB,
            .SecondaryKey = SDL_SCANCODE_UNKNOWN,
            .MouseButton = 0,
            .PadButton = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            .PadTriggerAxis = SDL_GAMEPAD_AXIS_INVALID
        });
    }

    [[nodiscard]] int GetWoodCost(Engine::RadialWheelItem item) const {
        return Sandbox::DeployableRegistry::ResolveWoodCost(item);
    }

    [[nodiscard]] glm::vec3 ComputePlacementPosition() const {
        auto* go = GetGameObject();
        if (!go) {
            return glm::vec3(0.0f);
        }
        const glm::vec3 origin = go->GetTransform()->GetWorldPosition();
        glm::vec2 aim{ 0.0f, 1.0f };
        if (auto* pc = go->GetComponent<PlayerController>()) {
            aim = pc->GetAimDirectionXZ();
        }
        const float len = glm::length(aim);
        if (std::isfinite(len) && len > 1e-4f) {
            aim /= len;
        } else {
            aim = { 0.0f, 1.0f };
        }
        return { origin.x + aim.x * PlacementDistance, 0.0f, origin.z + aim.y * PlacementDistance };
    }

    [[nodiscard]] bool IsPlacementClear(const glm::vec3& worldPos) const {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!scene) {
            return false;
        }
        std::vector<Engine::GameObject*> hits;
        const uint64_t mask = Engine::PhysicsLayer::Environment | Engine::PhysicsLayer::LowObstacle;
        Engine::PhysicsQueries::OverlapCircleXZ(*scene, { worldPos.x, worldPos.z }, ClearanceRadius, mask, hits);
        for (Engine::GameObject* hit : hits) {
            if (hit && hit != go && !hit->IsPendingDestroy()) {
                return false;
            }
        }
        return true;
    }

    Engine::GameObject* TryPlaceStructure(Engine::RadialWheelItem item) {
        if (item == Engine::RadialWheelItem::None) {
            return nullptr;
        }
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        auto* stats = go ? go->GetComponent<PlayerStatsComponent>() : nullptr;
        if (!go || !scene || !stats) {
            return nullptr;
        }

        const int cost = GetWoodCost(item);
        if (stats->Wood < cost) {
            return nullptr;
        }

        const glm::vec3 placePos = ComputePlacementPosition();
        if (!IsPlacementClear(placePos)) {
            return nullptr;
        }

        stats->Wood -= cost;
        Engine::GameObject* spawned = InstantiateStructure(*scene, item, placePos);
        if (!spawned) {
            stats->Wood += cost;
            return nullptr;
        }

        auto* deployable = spawned->GetComponent<DeployableStructureComponent>();
        if (!deployable) {
            deployable = spawned->AddComponent<DeployableStructureComponent>();
        }
        deployable->SetKind(item);
        return spawned;
    }

    void Update(float /*deltaTime*/) override {
        auto* go = GetGameObject();
        auto* scene = go ? go->GetScene() : nullptr;
        if (!go || !scene) {
            return;
        }

        auto* wheel = FindRadialWheel(*scene);
        if (wheel) {
            if (auto* stats = go->GetComponent<PlayerStatsComponent>()) {
                wheel->AvailableWood = static_cast<uint32_t>(std::max(0, stats->Wood));
            }
            if (Engine::Input::IsActionJustPressed("DropWheel") && !wheel->IsOpen) {
                wheel->OnConfirmDrop = [this](int /*wedge*/, Engine::RadialWheelItem item) {
                    TryPlaceStructure(item);
                };
                wheel->Open();
            } else if (wheel->IsOpen && !Engine::Input::IsActionPressed("DropWheel")) {
                if (wheel->ActiveWedge >= 0) {
                    wheel->ConfirmSelection(wheel->ActiveWedge);
                } else {
                    wheel->Close();
                }
            }
        }

        for (const auto& entry : Sandbox::DeployableRegistry::All()) {
            if (entry.Hotkey != SDL_SCANCODE_UNKNOWN && Engine::Input::IsKeyJustPressed(entry.Hotkey)) {
                TryPlaceStructure(entry.Item);
                break;
            }
        }
    }

private:

    [[nodiscard]] static Engine::UIRadialWheelComponent* FindRadialWheel(Engine::Scene& scene) noexcept {
        for (const auto& goPtr : scene.GetGameObjects()) {
            if (goPtr && !goPtr->IsPendingDestroy()) {
                if (auto* wheel = goPtr->GetComponent<Engine::UIRadialWheelComponent>()) {
                    return wheel;
                }
            }
        }
        return nullptr;
    }

    Engine::GameObject* InstantiateStructure(Engine::Scene& scene,
                                             Engine::RadialWheelItem item,
                                             const glm::vec3& worldPos) const {
        const auto* desc = Sandbox::DeployableRegistry::Find(item);
        std::string prefabRelPath;
        if (desc && Engine::GameplayData::IsInitialized()) {
            if (const auto* def = Engine::GameplayData::Drops().Find(desc->DropId)) {
                prefabRelPath = def->PrefabPath;
            }
        }
        if (prefabRelPath.empty() && desc && desc->FallbackPrefabPath) {
            prefabRelPath = desc->FallbackPrefabPath;
        }

        if (!prefabRelPath.empty()) {
            std::error_code ec;
            std::string resolved = Engine::ResourceManager::ResolvePath(prefabRelPath);
            if (!std::filesystem::exists(resolved, ec)) {
                resolved = Engine::ResourceManager::ResolvePath("assets/" + prefabRelPath);
            }
            if (std::filesystem::exists(resolved, ec)) {
                if (auto prefab = Engine::Prefab::LoadFromFile(resolved)) {
                    if (auto* inst = scene.InstantiatePrefab(*prefab, worldPos)) {
                        return inst;
                    }
                }
            }
        }

        const char* fallbackName = desc ? desc->DisplayName : "DeployableStructure";
        auto* fallbackGo = scene.CreateGameObject(fallbackName);
        fallbackGo->GetTransform()->position = worldPos;
        fallbackGo->AddComponent<Engine::SpriteRenderer>()->SetBillboardMode(Engine::SpriteRenderer::BillboardMode::ViewPlane);
        return fallbackGo;
    }
};

#endif // SANDBOX_STRUCTURE_PLACER_COMPONENT_H
