#ifndef TESTS_SCENE_LIFECYCLETESTHELPERS_H
#define TESTS_SCENE_LIFECYCLETESTHELPERS_H

#include "Engine/Scene/Component.h"

#include <functional>

namespace Engine::TestHelpers {

/// @brief Shared lifecycle counter component used across lifecycle tests (1 ComponentMask128 slot).
class LifecycleTrackerComponent : public Component {
public:
    int* externalDestroyCount = nullptr;
    int awakeCount = 0;
    int enableCount = 0;
    int disableCount = 0;
    int startCount = 0;
    int updateCount = 0;
    int fixedUpdateCount = 0;
    int lateUpdateCount = 0;
    int destroyCount = 0;

    void Awake() override { ++awakeCount; }
    void OnEnable() override { ++enableCount; }
    void OnDisable() override { ++disableCount; }
    void Start() override { ++startCount; }
    void Update(float) override { ++updateCount; }
    void FixedUpdate(float) override { ++fixedUpdateCount; }
    void LateUpdate(float) override { ++lateUpdateCount; }
    void OnDestroy() override {
        ++destroyCount;
        if (externalDestroyCount) {
            ++(*externalDestroyCount);
        }
    }
};

/// @brief Parameterized callback component replacing 10+ one-off mock components
/// to keep process-wide ComponentMask128 slot consumption minimal (1 ComponentMask128 slot).
class CallbackLifecycleComponent : public Component {
public:
    std::function<void(GameObject*)> onStart;
    std::function<void(GameObject*, float)> onUpdate;
    std::function<void(GameObject*, float)> onFixedUpdate;
    std::function<void(GameObject*)> onDestroy;

    void Start() override {
        if (onStart) {
            onStart(GetGameObject());
        }
    }

    void Update(float deltaTime) override {
        if (onUpdate) {
            onUpdate(GetGameObject(), deltaTime);
        }
    }

    void FixedUpdate(float fixedDeltaTime) override {
        if (onFixedUpdate) {
            onFixedUpdate(GetGameObject(), fixedDeltaTime);
        }
    }

    void OnDestroy() override {
        if (onDestroy) {
            onDestroy(GetGameObject());
        }
    }
};

} // namespace Engine::TestHelpers

#endif // TESTS_SCENE_LIFECYCLETESTHELPERS_H
