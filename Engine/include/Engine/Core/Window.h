#ifndef CORE_WINDOW_H
#define CORE_WINDOW_H

#include <string>
#include <SDL3/SDL.h>

namespace Engine {
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void PollEvents();
    void SwapBuffers();
    void Update();

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

    bool ShouldClose() const { return m_ShouldClose; }
    bool IsInitialized() const noexcept { return m_IsInitialized; }
    SDL_Window* GetNativeWindow() const { return m_Window; }
    SDL_Window* GetSDLWindow() const { return m_Window; }
    SDL_GLContext GetGLContext() const { return m_GLContext; }

private:
    SDL_Window* m_Window{nullptr};
    SDL_GLContext m_GLContext{nullptr};
    int m_Width{0};
    int m_Height{0};
    bool m_ShouldClose{false};
    bool m_IsInitialized{false};
};
} // namespace Engine

#endif // CORE_WINDOW_H
