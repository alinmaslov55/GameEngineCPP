#ifndef SANDBOX_RUN_TRANSITION_SERVICE_H
#define SANDBOX_RUN_TRANSITION_SERVICE_H

#include "Scripts/RunSessionManager.h"

#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneManager.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace Sandbox {

/// @brief Decoupled scene-transition service so gameplay scripts never include Scene/AlphaRunScene.h.
class RunTransitionService {
public:
    using RunSceneFactory = std::function<std::shared_ptr<Engine::Scene>(uint64_t seed)>;
    using HubSceneFactory = std::function<std::shared_ptr<Engine::Scene>()>;

    static void RegisterFactories(RunSceneFactory runFactory, HubSceneFactory hubFactory) {
        GetRunFactory() = std::move(runFactory);
        GetHubFactory() = std::move(hubFactory);
    }

    [[nodiscard]] static bool HasFactories() noexcept {
        return static_cast<bool>(GetRunFactory()) && static_cast<bool>(GetHubFactory());
    }

    static bool StartNewRun(float fadeSeconds = 0.45f) {
        auto& session = RunSessionManager::Get();
        session.ResetRunTelemetry();
        const uint64_t seed = session.NextRunSeed++;
        return StartRunWithSeed(seed, fadeSeconds);
    }

    static bool StartRunWithSeed(uint64_t seed, float fadeSeconds = 0.45f) {
        const auto& factory = GetRunFactory();
        if (!factory) {
            return false;
        }
        Engine::SceneManager::TransitionToScene([factory, seed]() {
            return factory(seed);
        }, fadeSeconds);
        return true;
    }

    static bool ReturnToSanctuary(float fadeSeconds = 0.45f) {
        const auto& factory = GetHubFactory();
        if (!factory) {
            return false;
        }
        Engine::SceneManager::TransitionToScene([factory]() {
            return factory();
        }, fadeSeconds);
        return true;
    }

private:
    static RunSceneFactory& GetRunFactory() {
        static RunSceneFactory instance;
        return instance;
    }

    static HubSceneFactory& GetHubFactory() {
        static HubSceneFactory instance;
        return instance;
    }
};

} // namespace Sandbox

#endif // SANDBOX_RUN_TRANSITION_SERVICE_H
