#ifndef SANDBOX_SCENE_BOOTSTRAPPER_H
#define SANDBOX_SCENE_BOOTSTRAPPER_H

#include "Scene/AlphaRunScene.h"

#include <Engine/Scene/Scene.h>

#include <functional>
#include <memory>
#include <vector>

namespace Sandbox {

/// @brief Strategy interface for binding runtime gameplay scripts to a loaded scene.
class ISceneBootstrapper {
public:
    virtual ~ISceneBootstrapper() = default;
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;
    [[nodiscard]] virtual bool Matches(const Engine::Scene& scene) const = 0;
    virtual void BindRuntimeScripts(const std::shared_ptr<Engine::Scene>& scene) const = 0;
};

class SanctuaryHubBootstrapper final : public ISceneBootstrapper {
public:
    [[nodiscard]] const char* GetName() const noexcept override { return "SanctuaryHubBootstrapper"; }
    [[nodiscard]] bool Matches(const Engine::Scene& scene) const override {
        return AlphaRunScene::IsSanctuaryHubScene(scene);
    }
    void BindRuntimeScripts(const std::shared_ptr<Engine::Scene>& scene) const override {
        AlphaRunScene::BindSanctuaryHubScripts(scene);
    }
};

class AlphaRunBootstrapper final : public ISceneBootstrapper {
public:
    [[nodiscard]] const char* GetName() const noexcept override { return "AlphaRunBootstrapper"; }
    [[nodiscard]] bool Matches(const Engine::Scene& scene) const override {
        return AlphaRunScene::IsAlphaScene(scene);
    }
    void BindRuntimeScripts(const std::shared_ptr<Engine::Scene>& scene) const override {
        AlphaRunScene::BindRuntimeGameplayScripts(scene);
    }
};

class DemoVillageBootstrapper final : public ISceneBootstrapper {
public:
    using BindCallback = std::function<void(const std::shared_ptr<Engine::Scene>&)>;

    explicit DemoVillageBootstrapper(BindCallback bindFn = nullptr)
        : m_BindFn(std::move(bindFn)) {}

    [[nodiscard]] const char* GetName() const noexcept override { return "DemoVillageBootstrapper"; }
    [[nodiscard]] bool Matches(const Engine::Scene& scene) const override {
        return !AlphaRunScene::IsSanctuaryHubScene(scene) && !AlphaRunScene::IsAlphaScene(scene);
    }
    void BindRuntimeScripts(const std::shared_ptr<Engine::Scene>& scene) const override {
        if (m_BindFn) {
            m_BindFn(scene);
        }
    }

private:
    BindCallback m_BindFn;
};

/// @brief Resolves the matching ISceneBootstrapper without if/else scene-type ladders in Editor/Runtime layers.
class SceneBootstrapRegistry {
public:
    static void RegisterFallbackDemoBinder(DemoVillageBootstrapper::BindCallback bindFn) {
        GetDemoBinder() = std::move(bindFn);
    }

    [[nodiscard]] static std::unique_ptr<ISceneBootstrapper> Resolve(const Engine::Scene& scene) {
        if (AlphaRunScene::IsSanctuaryHubScene(scene)) {
            return std::make_unique<SanctuaryHubBootstrapper>();
        }
        if (AlphaRunScene::IsAlphaScene(scene)) {
            return std::make_unique<AlphaRunBootstrapper>();
        }
        return std::make_unique<DemoVillageBootstrapper>(GetDemoBinder());
    }

    static void BindScene(const std::shared_ptr<Engine::Scene>& scene) {
        if (!scene) {
            return;
        }
        auto bootstrapper = Resolve(*scene);
        bootstrapper->BindRuntimeScripts(scene);
    }

private:
    static DemoVillageBootstrapper::BindCallback& GetDemoBinder() {
        static DemoVillageBootstrapper::BindCallback binder;
        return binder;
    }
};

} // namespace Sandbox

#endif // SANDBOX_SCENE_BOOTSTRAPPER_H
