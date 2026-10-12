#include "Engine/Core/Window.h"
#include <iostream>

namespace Engine {
Window::Window(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
        return;
    }

    m_Window = SDL_CreateWindow(title.c_str(), width, height, 0);
    if (!m_Window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
    }
}

Window::~Window() {
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
}
} // Engine
