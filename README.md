# Game Engine CPP (2.5D Diorama Engine)

A modular C++20 / OpenGL 4.1 / SDL3 / Box2D 2.5D "Paper-Cutout Diorama" game engine and Dear ImGui level editor inspired by *Cult of the Lamb* and *Don't Starve*.

## Engine Description

**Game Engine CPP** is a modern C++20 game engine and level editor built from the ground up for creating 2.5D "paper-cutout diorama" games. It combines the aesthetic charm of 2D illustrated sprite art with true 3D perspective cameras, real-time lighting, and physical environmental effects.

### Key Features
- **2.5D Cutout Standee Rendering**: Renders 2D character, prop, and foliage standees with camera-facing billboarding modes (ViewPlane, Cylindrical-Y, Flat-Ground), hardware depth-buffer sorting, and alpha testing.
- **Dynamic Lighting & Soft Shadows**: Directional sunlight with texel-snapped light space matrices and 9-tap soft PCF shadow filtering, plus multi-source 3D point lights with flame flicker simulation.
- **Stylized Splatmap Terrain**: 4-layer height-modulated terrain with real-time radial brush painting, smooth falloff blending, and disk PNG persistence.
- **Interactive Foliage & Wind Sway**: Bottom-anchored vertex-shader foliage swaying with travelling spatial wind waves, interactive player proximity push-parting, and volume-preserving squash-and-stretch spring physics.
- **HDR Post-Processing Stack**: 16-bit floating-point HDR composite pipeline with 5-mip Karis downsample / tent bloom, ACES Filmic and Storybook tone mapping, split-toning color grading, and diorama vignettes.
- **Integrated Dear ImGui Level Editor**: Dockable multi-panel workspace featuring 3D diorama viewport navigation, Hierarchy, Inspector, Tile Palette, splatmap painting tools, and an Asset Browser with automated chroma-key cutout art processing.
- **Modular Core Subsystems**: Box2D physics integration, isometric gravity arcs, SDL3 audio engine, JSON scene serialization, and comprehensive automated GoogleTest coverage across all modules.

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
