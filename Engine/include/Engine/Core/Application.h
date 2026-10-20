#ifndef CORE_APPLICATION_H
#define CORE_APPLICATION_H

#include <memory>
#include "Engine/Core/Window.h"

namespace Engine {
class Application {
public:
    Application();
    ~Application();

    void Run();

    bool IsRunning() const;
    void Update();
    void Render();
private:
    void Initialize();
    void Shutdown();

    std::unique_ptr<Window> m_Window;
    bool m_Running = true;
};
} // namespace Engine

#endif
