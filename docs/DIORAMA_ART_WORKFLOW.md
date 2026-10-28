# 2.5D Paper-Cutout Diorama Art Workflow Guide

This guide documents the end-to-end art authoring, chroma-key cutout processing, ground-plane alignment, wind sway tuning, and lighting workflow for the engine's **2.5D Paper-Cutout Diorama** style (*Cult of the Lamb* / *Don't Starve*).

---

## 1. Visual Style & Canvas Specifications

| Asset Category | Recommended Source Canvas | Filter & Wrap | Billboard / Render Mode |
| :--- | :--- | :--- | :--- |
| **Characters & NPCs** | `512x512` – `1024x1024` | Trilinear + Mipmaps, `CLAMP_TO_EDGE` | `BillboardMode::ViewPlane` |
| **Small Foliage & Grass** | `512x512` | Trilinear + Mipmaps, `CLAMP_TO_EDGE` | `BillboardMode::ViewPlane` |
| **Trees, Shrines & Banners** | `768x1024` – `1024x1024` | Trilinear + Mipmaps, `CLAMP_TO_EDGE` | `BillboardMode::ViewPlane` |
| **Ground Decals / Rugs** | `512x512` | Trilinear + Mipmaps, `CLAMP_TO_EDGE` | `BillboardMode::FlatGround` |
| **Terrain Splat Layers** | `512x512` – `1024x1024` | Trilinear + Mipmaps, `GL_REPEAT` | `TerrainComponent` (Seamless) |
| **Pixel-Art UI / Fonts** | `< 64x64` | `GL_NEAREST`, `CLAMP_TO_EDGE` | Screen-Space 2D UI Pass |

### Authoring Rules for Paper-Cutout Standees
1. **Bold Hand-Inked Outlines**: Paint props from a front-facing or slight `15°–25°` elevated three-quarter perspective with consistent dark-brown or charcoal-black ink outlines (`4px–8px` at `1024px` resolution).
2. **Do Not Paint Ground Cast Shadows into the Sprite**: The engine's **Pass 0 (`2048x2048` Directional ShadowMap)** dynamically projects real-time cutout silhouette shadows onto the splatmap terrain across `360°` sun angles and sways them in lockstep with foliage wind. Baked drop shadows will float or clash with scene lighting.
3. **Bottom-Center Ground Contact Pivot (`UV.y = 0` -> `Y = 0`)**:
   - Every upright standee (`DrawBillboard`) anchors its **bottom-center UV (`0.5, 0.0`)** directly at `Transform::position` (`Y = 0` ground plane).
   - If a PNG has empty transparent padding *below* the sprite's feet/roots, the sprite will appear to float above its ground shadow. Running the in-engine **`CutoutProcessor`** automatically crops transparent margins and **bottom-flush aligns** the lowest opaque pixel to row `height - 1` (`UV.y = 0.0`).

---

## 2. Background Keying, Alpha Cutoff & Mipmap Bleed

### Choosing a Background When Authoring or Generating Art
- **Native Transparent PNG (`PreserveExistingAlpha`)**: Ideal when exporting directly from Procreate, Photoshop, Krita, or Spine. Still run **Bottom-Flush & Alpha Bleed** so ground contact and mipmap edges are clean.
- **Chroma-Green (`#00FF00`)**: Recommended for warm/red/stone/wood/cloth subjects (e.g., `cult_banner.png`, `ritual_brazier.png`, `cult_shrine.png`).
- **Chroma-Magenta (`#FF00FF`)**: Recommended for lush green foliage, grass tufts, and pine trees (`tall_grass_tuft.png`) so emerald/olive leaf hues never overlap the background key color.

### Why Alpha-Edge RGB Bleed Matters
OpenGL generates trilinear mipmaps (`GL_LINEAR_MIPMAP_LINEAR`) by averaging `2x2` texel blocks. If `A == 0` transparent pixels surrounding a dark ink outline contain `#00FF00` green, `#FF00FF` magenta, or `#FFFFFF` white RGB values, distant mip levels blend that background color into silhouette edges—producing bright halos before `AlphaCutoff` (`0.18`) discards the fragment.

`Engine::CutoutProcessor` solves this automatically via **4-pass 8-connected RGB dilation**, extending opaque border RGB colors outward into `A == 0` pixels while keeping `A == 0` unchanged.

---

## 3. Using the In-Engine 2.5D Art Pipeline

### Method A: Interactive Asset Browser (ImGui Editor)
1. Place raw `.png` or `.jpg` art inside `Sandbox/assets/diorama/` (or any subfolder under `Sandbox/assets/`).
2. Open the **Asset Browser** panel in the Editor:
   - Browse live image thumbnails, filter by name, and hover to inspect pixel dimensions.
   - **Right-click** any image tile and choose:
     - **Process Cutout (Auto Key + Bottom-Flush + Bleed)** — auto-detects `#00FF00` green, `#FF00FF` magenta, solid backdrop, or existing alpha, removes interior holes, neutralizes color spill on anti-aliased outlines, bottom-flushes the root to `UV.y = 0`, applies 4px RGB alpha bleed, writes lossless `.png`, and hot-reloads `Texture` in-place across the open scene.
     - **Save Processed Copy (`<name>_cutout.png`)** — non-destructively writes a processed sibling PNG.
     - **Bottom-Flush & 4px Alpha Bleed Only (Keep Alpha)** — preserves hand-painted alpha while fixing bottom pivot alignment and mipmap edge bleed.
3. Select a **Drop Archetype** in the Asset Browser toolbar (*Auto-Detect*, *Swaying Foliage*, *Pine / Tree*, *Ritual Banner*, *Solid Standee*, or *Flat Ground Decal*) and **drag-and-drop** the thumbnail directly onto the **3D Scene Viewport**:
   - The editor raycasts the `Y = 0` ground plane at your cursor, computes natural world dimensions from the texture's aspect ratio, configures `SpriteRenderer`, `WindSwayWeight`, `BoxCollider2D`, and `SquashStretchComponent`, and selects the new entity in the Inspector.

### Method B: Headless CLI Batch Processing
For automated build scripts or external art tools, run `EngineSandbox` in headless CLI mode (no window or GPU context required):

```powershell
.\build\Sandbox\Debug\EngineSandbox.exe --process-cutout raw_art.jpg Sandbox/assets/diorama/prop_name.png
```

---

## 4. Recommended Component Tuning by Prop Archetype

| Prop Archetype | `WindSwayWeight` | `AlphaCutoff` | `Emissive` | Collider & Secondary Motion |
| :--- | :---: | :---: | :---: | :--- |
| **Tall Grass / Wildflowers** | `0.80` – `0.95` | `0.18` | `0.00` | No blocking collider (walkthrough interactive parting!) |
| **Berry Bush / Shrub** | `0.60` – `0.75` | `0.18` | `0.00` | Optional or walkthrough |
| **Storybook Pine / Oak Tree** | `0.32` – `0.45` | `0.18` | `0.00` | Static `BoxCollider2D` (`0.55 x 0.45` at trunk base) |
| **Cult Banner / Standard** | `0.65` – `0.80` | `0.18` | `0.15` – `0.25` | Static `BoxCollider2D` (`0.45 x 0.40`) + `SquashStretchComponent` |
| **Stone Shrine / Brazier** | `0.00` (Rigid) | `0.18` | `0.25` – `0.90` | Static `BoxCollider2D` + `PointLightComponent` (Flicker ON) |
| **Hero / NPC Character** | `0.00` (Rigid) | `0.18` | `0.00` | Dynamic `BoxCollider2D` + `SquashStretchComponent` (Walk Hop ON) |

