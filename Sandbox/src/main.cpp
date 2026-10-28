#include <Engine/Core/Application.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <memory>

#ifdef ENGINE_BUILD_EDITOR
#include "Editor/EditorLayer.h"
#else
#include "Runtime/RuntimeLayer.h"
#endif

int main(int argc, char* argv[]) {
    Engine::Application app;

#ifdef ENGINE_BUILD_EDITOR
    app.PushLayer(std::make_unique<Sandbox::EditorLayer>());
#else
    const char* scenePath = (argc > 1 && argv[1] && argv[1][0] != '\0') ? argv[1] : "Village.scene";
    auto runtimeLayer = std::make_unique<Sandbox::RuntimeLayer>(scenePath);
    auto* runtimeLayerPtr = runtimeLayer.get();
    app.PushLayer(std::move(runtimeLayer));
    if (runtimeLayerPtr->HasStartupFailed()) {
        return EXIT_FAILURE;
    }
#endif

    app.Run();
    return EXIT_SUCCESS;
}
