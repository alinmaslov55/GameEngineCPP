#ifndef CORE_APPLICATION_H
#define CORE_APPLICATION_H

#include <functional>
#include <memory>
#include <vector>
#include "Engine/Core/Layer.h"
#include "Engine/Core/Window.h"

namespace Engine {
class Application {
public:
    Application();
    ~Application();

    static Application& Get() { return *s_Instance; }

    void Run();

    void PushLayer(std::unique_ptr<Layer> layer);

    bool IsRunning() const;
    void Close();
    void Update();
    void Render();

    Window& GetWindow() { return *m_Window; }
    const Window& GetWindow() const { return *m_Window; }

#ifdef ENGINE_BUILD_EDITOR
    void SetImGuiRenderCallback(std::function<void()> callback) { m_ImGuiRenderCallback = std::move(callback); }
#endif

private:
    void Initialize();
    void Shutdown();

    static Application* s_Instance;

    std::unique_ptr<Window> m_Window;
    std::vector<std::unique_ptr<Layer>> m_Layers;
    bool m_Running = true;
#ifdef ENGINE_BUILD_EDITOR
    std::function<void()> m_ImGuiRenderCallback{nullptr};
#endif
};
} // namespace Engine

#endif // CORE_APPLICATION_H
