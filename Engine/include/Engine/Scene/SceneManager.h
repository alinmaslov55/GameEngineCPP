#ifndef SCENE_SCENE_MANAGER_H
#define SCENE_SCENE_MANAGER_H

#include "Engine/Scene/Scene.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Engine {

enum class SceneTransitionState : uint8_t {
    None = 0,
    FadeOut,
    FadeIn
};

using SceneFactoryFn = std::function<std::shared_ptr<Scene>()>;

class SceneManager {
public:
    static void LoadScene(std::shared_ptr<Scene> scene);
    static void Shutdown();
    static std::shared_ptr<Scene> GetActiveScene();

    static void Update(float deltaTime);
    static void UpdateRuntime(float deltaTime);
    static void OnRuntimeStart();
    static void OnRuntimeStop();
    static void Render(const PerspectiveCamera* overrideCamera = nullptr);

    // Async Fade Transition API
    static void TransitionToScene(SceneFactoryFn nextSceneFactory, float fadeDurationSec = 0.45f);
    static void UpdateTransition(float deltaTime);
    [[nodiscard]] static bool IsTransitioning() noexcept;
    [[nodiscard]] static float GetFadeAlpha() noexcept;
    [[nodiscard]] static SceneTransitionState GetTransitionState() noexcept;
    static void CancelTransition() noexcept;

private:
    static std::shared_ptr<Scene> s_ActiveScene;

    static SceneTransitionState s_TransitionState;
    static SceneFactoryFn s_PendingSceneFactory;
    static float s_FadeDuration;
    static float s_HalfDuration;
    static float s_FadeTimer;
    static float s_FadeAlpha;
};

} // namespace Engine

#endif // SCENE_SCENE_MANAGER_H
