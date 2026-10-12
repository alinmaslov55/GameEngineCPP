#ifndef CORE_WINDOW_H
#define CORE_WINDOW_H

#include <string>
#include <SDL3/SDL.h>

namespace Engine {
namespace Core {
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    void Update();
    bool ShouldClose() const { return m_ShouldClose; }

private:
    SDL_Window* m_Window;
    bool m_ShouldClose{false};
};
} // Core
} // Engine

#endif // CORE_WINDOW_H
