# Game Engine CPP (2.5D Diorama Engine)

A modular C++20 / OpenGL 4.1 / SDL3 / Box2D 2.5D "Paper-Cutout Diorama" game engine and Dear ImGui level editor inspired by *Cult of the Lamb* and *Don't Starve*.

## Architecture Overview

- `Engine/`: Core static library (`Layer` stack, `PerspectiveCamera` & `OrthographicCamera`, `2048x2048` 24-bit directional cutout `ShadowMap` + 9-tap soft PCF 2.5D lighting across 4-layer height-modulated splatmap `TerrainRenderer` and 5-pass hardware depth & alpha-cutout `Renderer2D`, `DirectionalLightComponent` & `PointLightComponent`, `Scene` / `SceneSerializer`, Box2D + 3D ground physics, `ResourceManager`, SDL3 audio, and optional Dear ImGui DockBuilder workspace).
- `Sandbox/`: Modular `EditorLayer` + 7 dockable `Panels/` (including Time-of-Day lighting presets, flickering 3D point lights, real-time 3D radial splatmap brush painting & live ground ring cursor) when `ENGINE_BUILD_EDITOR=ON`, or a lightweight standalone `RuntimeLayer` client (`ENGINE_DIST`) when `ENGINE_BUILD_EDITOR=OFF`.
- `Tests/`: Automated GoogleTest suite (`EngineTests`).

## Building & Running

### 1. Editor Build (`ENGINE_BUILD_EDITOR=ON`, Default)
```powershell
cmake --preset editor
cmake --build --preset editor-debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Sandbox\Debug\EngineSandbox.exe
```

### 2. Standalone Player Distribution Build (`ENGINE_BUILD_EDITOR=OFF`)
```powershell
cmake --preset dist
cmake --build --preset dist-release
.\build-dist\Sandbox\Release\EngineSandbox.exe
```

## Future Improvements & Roadmap

- **Toggleable 2D Isometric / 2.5D Diorama Camera & Render Mode**: Allow switching scenes dynamically in the Editor and at runtime between classic 2D orthographic isometric projection (`Isometric::WorldToScreen`) and 3D perspective diorama cutout billboarding.
- **Phase 4 — HDR Post-Processing**: `GL_RGBA16F` framebuffer pass with thresholded Gaussian/Kawase Bloom, warm/cool color grading, and atmospheric vignette.
- **Phase 5 — GPU Foliage Wind & Secondary Motion**: Vertex-shader wind wave displacement for grass, crops, and banners plus squash-and-stretch character springs.
