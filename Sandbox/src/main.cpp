#include "Editor/EditorLayer.h"

#include <Engine/Core/Application.h>
#include <memory>

int main(int argc, char* argv[]) {
    Engine::Application app;
    app.PushLayer(std::make_unique<Sandbox::EditorLayer>());
    app.Run();
    return 0;
}
