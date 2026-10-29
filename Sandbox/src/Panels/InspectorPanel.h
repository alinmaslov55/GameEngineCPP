#ifndef SANDBOX_INSPECTORPANEL_H
#define SANDBOX_INSPECTORPANEL_H

namespace Engine {
class GameObject;
class Transform;
class SpriteRenderer;
class ParticleSystem;
class DirectionalLightComponent;
class PointLightComponent;
class PostProcessComponent;
class WindComponent;
class SquashStretchComponent;
class Rigidbody2D;
class BoxCollider2D;
class CameraComponent;
class TerrainComponent;
class Tilemap;
} // namespace Engine

namespace Sandbox {

class InspectorPanel {
public:
    void OnImGuiRender(Engine::GameObject* selectedEntity, bool* pOpen);

private:
    static void DrawEntityHeader(Engine::GameObject* entity);
    static void DrawTransform(Engine::Transform* transform);
    static void DrawSpriteRenderer(Engine::SpriteRenderer& spriteRenderer, Engine::GameObject* entity);
    static void DrawParticleSystem(Engine::ParticleSystem& particleSystem, Engine::GameObject* entity);
    static void DrawDirectionalLight(Engine::DirectionalLightComponent& dirLight, Engine::GameObject* entity);
    static void DrawPointLight(Engine::PointLightComponent& pointLight, Engine::GameObject* entity);
    static void DrawPostProcess(Engine::PostProcessComponent& pp, Engine::GameObject* entity);
    static void DrawWindZone(Engine::WindComponent& wind, Engine::GameObject* entity);
    static void DrawSquashStretch(Engine::SquashStretchComponent& ss, Engine::GameObject* entity);
    static void DrawRigidbody2D(Engine::Rigidbody2D& rb2d, Engine::GameObject* entity);
    static void DrawBoxCollider2D(Engine::BoxCollider2D& boxCollider, Engine::GameObject* entity);
    static void DrawCamera(Engine::CameraComponent& camComp, Engine::GameObject* entity);
    static void DrawTerrain(Engine::TerrainComponent& terrain, Engine::GameObject* entity);
    static void DrawTilemap(Engine::Tilemap& tilemapComp, Engine::GameObject* entity);
    static void DrawAddComponentMenu(Engine::GameObject* entity);
};

} // namespace Sandbox

#endif // SANDBOX_INSPECTORPANEL_H
