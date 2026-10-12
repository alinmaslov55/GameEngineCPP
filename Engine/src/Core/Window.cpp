#include "Engine/Core/Window.h"
#include <glad/glad.h>
#include <iostream>

namespace Engine {
Window::Window(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    m_Window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL);
    if (!m_Window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
        return;
    }

    m_GLContext = SDL_GL_CreateContext(m_Window);
    if (!m_GLContext) {
        std::cerr << "Failed to create GL context: " << SDL_GetError() << '\n';
        return;
    }

    SDL_GL_MakeCurrent(m_Window, m_GLContext);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
    }
}

Window::~Window() {
    if (m_GLContext) {
        SDL_GL_DestroyContext(m_GLContext);
    }
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Window::Update() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            m_ShouldClose = true;
        }
    }
    
    SDL_GL_SwapWindow(m_Window);
}
} // namespace Engine