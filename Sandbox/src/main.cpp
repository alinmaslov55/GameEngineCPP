#include <Engine/Core/Application.h>
#include <Engine/Utils/CutoutProcessor.h>
#include <Engine/Utils/Logger.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

#ifdef ENGINE_BUILD_EDITOR
#include "Editor/EditorLayer.h"
#else
#include "Runtime/RuntimeLayer.h"
#endif

int main(int argc, char* argv[]) {
    // Headless 2.5D Art Pipeline CLI mode: EngineSandbox --process-cutout <input> <output.png>
    if (argc >= 4 && argv[1] && std::string_view(argv[1]) == "--process-cutout") {
        Engine::Logger::Init();
        Engine::CutoutProcessSettings settings;
        const auto result = Engine::CutoutProcessor::ProcessFile(argv[2], argv[3], settings);
        if (!result.Success) {
            ENGINE_ERROR("CutoutProcessor failed: {}", result.ErrorMessage);
            std::fprintf(stderr, "CutoutProcessor failed: %s\n", result.ErrorMessage.c_str());
            return EXIT_FAILURE;
        }
        ENGINE_INFO("Processed '{}' -> '{}' ({}x{}, aspect {:.3f})",
                    argv[2], argv[3], result.OutputWidth, result.OutputHeight, result.SuggestedAspectRatio);
        std::printf("Processed '%s' -> '%s' (%ux%u, aspect %.3f)\n",
                    argv[2], argv[3], result.OutputWidth, result.OutputHeight, result.SuggestedAspectRatio);
        return EXIT_SUCCESS;
    }

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
