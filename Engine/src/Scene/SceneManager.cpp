#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Time.h"
#include "Engine/Renderer/Renderer2D.h"
#include "Engine/Renderer/RHI/Renderer.h"
#include "Engine/Utils/Logger.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

std::shared_ptr<Scene> SceneManager::s_ActiveScene{nullptr};

SceneTransitionState SceneManager::s_TransitionState = SceneTransitionState::None;
SceneFactoryFn SceneManager::s_PendingSceneFactory = nullptr;
float SceneManager::s_FadeDuration = 0.45f;
float SceneManager::s_HalfDuration = 0.225f;
float SceneManager::s_FadeTimer = 0.0f;
float SceneManager::s_FadeAlpha = 0.0f;

void SceneManager::LoadScene(std::shared_ptr<Scene> scene) {
    CancelTransition();
    s_ActiveScene = scene;
    if (s_ActiveScene) {
        ENGINE_CORE_INFO("SceneManager: Loaded scene '{}'", s_ActiveScene->GetName());
    } else {
        ENGINE_CORE_WARN("SceneManager: Loaded null scene");
    }
}

void SceneManager::Shutdown() {
    CancelTransition();
    if (s_ActiveScene) {
        ENGINE_CORE_INFO("SceneManager: Unloading active scene '{}'", s_ActiveScene->GetName());
        if (s_ActiveScene->IsRuntimeRunning()) {
            s_ActiveScene->OnRuntimeStop();
        }
        s_ActiveScene.reset();
    }
}

std::shared_ptr<Scene> SceneManager::GetActiveScene() {
    return s_ActiveScene;
}

void SceneManager::TransitionToScene(SceneFactoryFn nextSceneFactory, float fadeDurationSec) {
    if (!nextSceneFactory) {
        ENGINE_CORE_ERROR("SceneManager::TransitionToScene: Provided null nextSceneFactory");
        return;
    }

    if (s_TransitionState != SceneTransitionState::None) {
        ENGINE_CORE_WARN("SceneManager::TransitionToScene: Transition already in progress, superseding target factory");
    }

    s_PendingSceneFactory = std::move(nextSceneFactory);
    s_FadeDuration = std::max(fadeDurationSec, 0.02f);
    s_HalfDuration = s_FadeDuration * 0.5f;
    s_FadeTimer = 0.0f;
    s_FadeAlpha = 0.0f;
    s_TransitionState = SceneTransitionState::FadeOut;

    ENGINE_CORE_INFO("SceneManager::TransitionToScene: Beginning fade out transition (duration={:.2f}s)", s_FadeDuration);
}

void SceneManager::UpdateTransition(float deltaTime) {
    if (s_TransitionState == SceneTransitionState::None) {
        return;
    }

    s_FadeTimer += deltaTime;

    if (s_TransitionState == SceneTransitionState::FadeOut) {
        s_FadeAlpha = std::clamp(s_FadeTimer / s_HalfDuration, 0.0f, 1.0f);
        if (s_FadeTimer >= s_HalfDuration) {
            ENGINE_CORE_INFO("SceneManager: Peak blackout reached, performing scene swap...");

            bool wasRuntimeRunning = s_ActiveScene && s_ActiveScene->IsRuntimeRunning();
            if (wasRuntimeRunning) {
                s_ActiveScene->OnRuntimeStop();
            }

            std::shared_ptr<Scene> newScene = nullptr;
            if (s_PendingSceneFactory) {
                newScene = s_PendingSceneFactory();
            }

            s_ActiveScene = newScene;
            if (s_ActiveScene) {
                ENGINE_CORE_INFO("SceneManager: Swapped to new scene '{}'", s_ActiveScene->GetName());
                if (wasRuntimeRunning) {
                    s_ActiveScene->OnRuntimeStart();
                }
            } else {
                ENGINE_CORE_WARN("SceneManager: Swapped to null scene");
            }

            s_PendingSceneFactory = nullptr;
            s_TransitionState = SceneTransitionState::FadeIn;
            s_FadeTimer = 0.0f;
            s_FadeAlpha = 1.0f;
        }
    } else if (s_TransitionState == SceneTransitionState::FadeIn) {
        s_FadeAlpha = std::clamp(1.0f - (s_FadeTimer / s_HalfDuration), 0.0f, 1.0f);
        if (s_FadeTimer >= s_HalfDuration) {
            s_FadeAlpha = 0.0f;
            s_TransitionState = SceneTransitionState::None;
            s_FadeTimer = 0.0f;
            ENGINE_CORE_INFO("SceneManager: Scene transition complete.");
        }
    }
}

bool SceneManager::IsTransitioning() noexcept {
    return s_TransitionState != SceneTransitionState::None;
}

float SceneManager::GetFadeAlpha() noexcept {
    return s_FadeAlpha;
}

SceneTransitionState SceneManager::GetTransitionState() noexcept {
    return s_TransitionState;
}

void SceneManager::CancelTransition() noexcept {
    s_TransitionState = SceneTransitionState::None;
    s_PendingSceneFactory = nullptr;
    s_FadeTimer = 0.0f;
    s_FadeAlpha = 0.0f;
}

void SceneManager::Update(float deltaTime) {
    ENGINE_CORE_TRACE("SceneManager::Update");
    const float unscaledDt = Time::GetUnscaledDeltaTime();
    UpdateTransition(unscaledDt > 0.0f ? unscaledDt : deltaTime);
    if (s_ActiveScene) {
        s_ActiveScene->Update(deltaTime);
    }
}

void SceneManager::UpdateRuntime(float deltaTime) {
    ENGINE_CORE_TRACE("SceneManager::UpdateRuntime");
    const float unscaledDt = Time::GetUnscaledDeltaTime();
    UpdateTransition(unscaledDt > 0.0f ? unscaledDt : deltaTime);
    if (s_ActiveScene) {
        s_ActiveScene->UpdateRuntime(deltaTime);
    }
}

void SceneManager::OnRuntimeStart() {
    if (s_ActiveScene) {
        s_ActiveScene->OnRuntimeStart();
    }
}

void SceneManager::OnRuntimeStop() {
    if (s_ActiveScene) {
        s_ActiveScene->OnRuntimeStop();
    }
}

void SceneManager::Render(const PerspectiveCamera* overrideCamera) {
    ENGINE_CORE_TRACE("SceneManager::Render");
    if (s_ActiveScene) {
        s_ActiveScene->Render(overrideCamera);
    }

    if (s_FadeAlpha > 0.001f && Renderer2D::IsInitialized()) {
        Renderer2D::BeginScene(glm::mat4(1.0f), false);
        const glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 1.0f));
        Renderer2D::DrawQuad(transform, glm::vec4(0.0f, 0.0f, 0.0f, s_FadeAlpha));
        Renderer2D::EndScene();
    }
}

} // namespace Engine
