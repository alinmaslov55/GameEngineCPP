#include "Engine/Core/Window.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>

namespace Engine {
Window::Window(const std::string& title, int width, int height) {
    ENGINE_CORE_INFO("Creating window '{}' ({}x{})", title, width, height);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ENGINE_CORE_CRITICAL("Failed to initialize SDL: {}", SDL_GetError());
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    m_Window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL);
    if (!m_Window) {
        ENGINE_CORE_CRITICAL("Failed to create window: {}", SDL_GetError());
        return;
    }

    m_GLContext = SDL_GL_CreateContext(m_Window);
    if (!m_GLContext) {
        ENGINE_CORE_CRITICAL("Failed to create GL context: {}", SDL_GetError());
        return;
    }

    SDL_GL_MakeCurrent(m_Window, m_GLContext);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        ENGINE_CORE_CRITICAL("Failed to initialize GLAD");
        return;
    }

    ENGINE_CORE_INFO("OpenGL Context Initialized: Version {}.{}", GLVersion.major, GLVersion.minor);
    ENGINE_CORE_INFO("Window '{}' created successfully (VSync enabled)", title);
}

Window::~Window() {
    ENGINE_CORE_INFO("Destroying window...");
    if (m_GLContext) {
        SDL_GL_DestroyContext(m_GLContext);
    }
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    ENGINE_CORE_INFO("Window destroyed");
}

void Window::PollEvents() {
    ENGINE_CORE_TRACE("Window::PollEvents");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            ENGINE_CORE_INFO("Window close event received");
            m_ShouldClose = true;
        }
    }
}

void Window::SwapBuffers() {
    ENGINE_CORE_TRACE("Window::SwapBuffers");
    SDL_GL_SwapWindow(m_Window);
}

void Window::Update() {
    PollEvents();
    SwapBuffers();
}
} // namespace Engine
