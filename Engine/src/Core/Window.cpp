#include "Engine/Core/Window.h"
#include "Engine/Core/Input.h"
#include "Engine/Utils/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#ifdef ENGINE_BUILD_EDITOR
#include <imgui_impl_sdl3.h>
#endif

namespace Engine {
Window::Window(const std::string& title, int width, int height)
    : m_Width(width), m_Height(height), m_FramebufferWidth(width), m_FramebufferHeight(height) {
    ENGINE_CORE_INFO("Creating window '{}' ({}x{})", title, width, height);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ENGINE_CORE_CRITICAL("Failed to initialize SDL: {}", SDL_GetError());
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    m_Window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_Window) {
        ENGINE_CORE_CRITICAL("Failed to create window: {}", SDL_GetError());
        return;
    }

    SDL_GetWindowSizeInPixels(m_Window, &m_FramebufferWidth, &m_FramebufferHeight);

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

    m_IsInitialized = true;
    ENGINE_CORE_INFO("OpenGL Context Initialized: Version {}.{}", GLVersion.major, GLVersion.minor);
    ENGINE_CORE_INFO("Window '{}' created successfully (VSync enabled)", title);
}

Window::~Window() {
    ENGINE_CORE_INFO("Destroying window...");
    m_IsInitialized = false;
    if (m_GLContext) {
        SDL_GL_MakeCurrent(m_Window, nullptr);
        SDL_GL_DestroyContext(m_GLContext);
        m_GLContext = nullptr;
    }
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    ENGINE_CORE_INFO("Window destroyed");
}

void Window::PollEvents() {
    Input::BeginFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifdef ENGINE_BUILD_EDITOR
        ImGui_ImplSDL3_ProcessEvent(&event);
#endif
        switch (event.type) {
            case SDL_EVENT_QUIT:
                ENGINE_CORE_INFO("Window close event received");
                m_ShouldClose = true;
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                if (event.window.data1 > 0 && event.window.data2 > 0) {
                    m_FramebufferWidth = event.window.data1;
                    m_FramebufferHeight = event.window.data2;
                    ENGINE_CORE_DEBUG("Window framebuffer resized to {}x{}", m_FramebufferWidth, m_FramebufferHeight);
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                if (event.window.data1 > 0 && event.window.data2 > 0) {
                    m_Width = event.window.data1;
                    m_Height = event.window.data2;
#ifndef ENGINE_BUILD_EDITOR
                    Input::SetViewportRect(
                        { 0.0f, 0.0f },
                        { static_cast<float>(m_Width), static_cast<float>(m_Height) }
                    );
#endif
                    ENGINE_CORE_DEBUG("Window resized to {}x{}", m_Width, m_Height);
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL: {
                const float wheelDir = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
                Input::OnMouseWheel(event.wheel.x * wheelDir, event.wheel.y * wheelDir);
                break;
            }

            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat) {
                    Input::OnKeyEvent(event.key.scancode, true);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                Input::OnMouseButtonEvent(event.button.button, true);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                Input::OnMouseMotionEvent(event.motion.xrel, event.motion.yrel);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (Input::IsActiveGamepad(event.gbutton.which)) {
                    Input::OnGamepadButtonEvent(static_cast<SDL_GamepadButton>(event.gbutton.button), true);
                }
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                if (Input::IsActiveGamepad(event.gaxis.which)) {
                    const float normalized = Input::NormalizeGamepadAxis(event.gaxis.value);
                    Input::OnGamepadAxisEvent(static_cast<SDL_GamepadAxis>(event.gaxis.axis), normalized);
                }
                break;
            }

            case SDL_EVENT_GAMEPAD_ADDED:
                Input::OnGamepadAdded(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                Input::OnGamepadRemoved(event.gdevice.which);
                break;

            default:
                break;
        }
    }

    Input::Update();
}

void Window::SwapBuffers() {
    SDL_GL_SwapWindow(m_Window);
}

void Window::Update() {
    PollEvents();
    SwapBuffers();
}
} // namespace Engine
