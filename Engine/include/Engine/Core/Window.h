#ifndef CORE_WINDOW_H
#define CORE_WINDOW_H

#include <string>
#include <SDL3/SDL.h>

namespace Engine {
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    void PollEvents();
    void SwapBuffers();
    void Update();

    bool ShouldClose() const { return m_ShouldClose; }
    SDL_Window* GetNativeWindow() const { return m_Window; }

private:
    SDL_Window* m_Window;
    SDL_GLContext m_GLContext;
    bool m_ShouldClose{false};
};
} // namespace Engine

#endif // CORE_WINDOW_H
