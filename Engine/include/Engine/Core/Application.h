#ifndef CORE_APPLICATION_H
#define CORE_APPLICATION_H

#include <memory>
#include "Engine/Core/Window.h"

namespace Engine {
namespace Core {
class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    void Initialize();
    void Update();
    void Render();
    void Shutdown();

    std::unique_ptr<Window> m_Window;
    bool m_Running = true;
};
} // namespace Core
} // namespace Engine

#endif
