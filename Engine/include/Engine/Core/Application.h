#ifndef CORE_APPLICATION_H
#define CORE_APPLICATION_H

#include <memory>
#include <functional>
#include "Engine/Core/Window.h"

namespace Engine {
class Application {
public:
    Application();
    ~Application();

    void Run();

    bool IsRunning() const;
    void Close();
    void Update();
    void Render();

    Window& GetWindow() { return *m_Window; }
    const Window& GetWindow() const { return *m_Window; }

    void SetImGuiRenderCallback(std::function<void()> callback) { m_ImGuiRenderCallback = std::move(callback); }

private:
    void Initialize();
    void Shutdown();

    std::unique_ptr<Window> m_Window;
    bool m_Running = true;
    std::function<void()> m_ImGuiRenderCallback{nullptr};
};
} // namespace Engine

#endif
