# Game Engine CPP (2.5D Diorama Engine)

A modular C++20 / OpenGL 4.1 / SDL3 / Box2D 2.5D "Paper-Cutout Diorama" game engine and Dear ImGui level editor inspired by *Cult of the Lamb* and *Don't Starve*.

## Architecture Overview

- `Engine/`: Core static library (`Layer` stack, `PerspectiveCamera` & `OrthographicCamera`, `GL_RGBA16F` `PostProcessor` with 5-mip Karis/Tent HDR Bloom + ACES/Storybook Tone Mapping + Split-Toning Color Grading + Vignette, `2048x2048` 24-bit directional cutout `ShadowMap` + 9-tap soft PCF 2.5D lighting, bottom-anchored vertex-shader `WindComponent` foliage sway + interactive player parting + volume-preserving `SquashStretchComponent`, C++ `CutoutProcessor` 2.5D art pipeline, 4-layer height-modulated splatmap `TerrainRenderer`, 5-pass hardware depth & alpha-cutout `Renderer2D`, `Scene` / `SceneSerializer`, Box2D + 3D ground physics, `ResourceManager`, SDL3 audio, and optional Dear ImGui DockBuilder workspace).
- `Sandbox/`: Modular `EditorLayer` + 7 dockable `Panels/` (including Thumbnail Asset Browser with right-click 2.5D Cutout Processor & 3D Viewport drag-and-drop spawning, Wind & Squash/Stretch controls, HDR Post-Process mood presets, Time-of-Day lighting presets, flickering 3D point lights, and real-time 3D radial splatmap brush painting) when `ENGINE_BUILD_EDITOR=ON`, or a lightweight standalone `RuntimeLayer` client (`ENGINE_DIST`) when `ENGINE_BUILD_EDITOR=OFF`.
- `docs/`: Technical & artist guides including [`DIORAMA_ART_WORKFLOW.md`](docs/DIORAMA_ART_WORKFLOW.md).
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

### 3. Headless 2.5D Cutout Art Processing CLI
```powershell
.\build\Sandbox\Debug\EngineSandbox.exe --process-cutout raw_art.jpg Sandbox/assets/diorama/prop_name.png
```
See [`docs/DIORAMA_ART_WORKFLOW.md`](docs/DIORAMA_ART_WORKFLOW.md) for canvas specs, chroma-key rules, bottom-flush ground pivots, alpha-edge bleed, and wind sway guidelines.

## Future Improvements & Roadmap

- **Toggleable 2D Isometric / 2.5D Diorama Camera & Render Mode**: Allow switching scenes dynamically in the Editor and at runtime between classic 2D orthographic isometric projection (`Isometric::WorldToScreen`) and 3D perspective diorama cutout billboarding.
