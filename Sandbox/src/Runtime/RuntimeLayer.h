#ifndef SANDBOX_RUNTIMELAYER_H
#define SANDBOX_RUNTIMELAYER_H

#include <Engine/Core/Layer.h>
#include <Engine/Scene/Scene.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Sandbox {

class RuntimeLayer : public Engine::Layer {
public:
    explicit RuntimeLayer(std::string startupScenePath = "Village.scene");
    ~RuntimeLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;

    bool HasStartupFailed() const { return m_StartupFailed; }

private:
    std::string m_StartupScenePath;
    std::shared_ptr<Engine::Scene> m_ActiveScene;
    uint32_t m_ViewportWidth = 0;
    uint32_t m_ViewportHeight = 0;
    bool m_StartupFailed = false;
};

} // namespace Sandbox

#endif // SANDBOX_RUNTIMELAYER_H

