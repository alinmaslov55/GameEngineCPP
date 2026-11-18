#include "Engine/UI/MinimapFogSystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Components/LightComponents.h"
#include "Engine/Scene/GameObject.h"
#include <algorithm>
#include <cmath>

namespace Engine {

MinimapFogSystem::MinimapFogSystem() {
    m_ToroidalGrid.resize(TextureSize * TextureSize);
    m_DisplayBuffer.resize(TextureSize * TextureSize * 4, 0);

    // Create GPU texture (safe even in headless mode without GL context)
    m_FogTexture = std::make_shared<Texture>(TextureSize, TextureSize, m_DisplayBuffer.data(), /*linearFilter=*/true, /*repeatWrap=*/false);
    Reset();
}

void MinimapFogSystem::Reset() {
    for (auto& cell : m_ToroidalGrid) {
        cell.BiomeColorID = BiomeToColorID(BiomeType::BlackwoodThickets);
        cell.Discovered = 0;
        cell.LiveLit = 0;
    }

    m_CurrentViewerWorldXZ = { 0.0f, 0.0f };
    m_CurrentGridCenter = { 0, 0 };
    m_ActivePins.clear();
    RebuildDisplayBuffer();
    m_TextureDirty = true;
}

uint8_t MinimapFogSystem::BiomeToColorID(BiomeType biome) noexcept {
    switch (biome) {
        case BiomeType::BlackwoodThickets: return 35;  // Deep emerald forest
        case BiomeType::AshBarrens:        return 110; // Ash grey
        case BiomeType::TarBogs:           return 20;  // Tar dark violet
        case BiomeType::ChalkHighlands:    return 220; // Chalk bone white
        case BiomeType::SanctuaryCobble:   return 180; // Cobblestone amber
        default:                           return 40;
    }
}

glm::ivec2 MinimapFogSystem::WorldToGridCoord(const glm::vec2& worldXZ) noexcept {
    const int gx = static_cast<int>(std::floor(worldXZ.x / TexelWorldSize));
    const int gz = static_cast<int>(std::floor(worldXZ.y / TexelWorldSize));
    return { gx, gz };
}

glm::ivec2 MinimapFogSystem::GridToToroidalTexel(const glm::ivec2& gridCoord) noexcept {
    const int size = static_cast<int>(TextureSize);
    const int tx = (gridCoord.x % size + size) % size;
    const int tz = (gridCoord.y % size + size) % size;
    return { tx, tz };
}

void MinimapFogSystem::Update(const glm::vec2& viewerWorldXZ,
                             float heroTorchRadius,
                             const std::vector<glm::vec3>& lightSources,
                             const WorldBiomeGenerator* biomeGen) {
    const glm::ivec2 newCenter = WorldToGridCoord(viewerWorldXZ);
    const glm::ivec2 oldCenter = m_CurrentGridCenter;

    // 1. Toroidal window scrolling: update biomes and clear wrapped-out territory
    if (newCenter != oldCenter) {
        const int deltaX = newCenter.x - oldCenter.x;
        const int deltaZ = newCenter.y - oldCenter.y;

        // If moved more than the full window, reset all cells
        if (std::abs(deltaX) >= static_cast<int>(TextureSize) || std::abs(deltaZ) >= static_cast<int>(TextureSize)) {
            for (int rz = -128; rz < 128; ++rz) {
                for (int rx = -128; rx < 128; ++rx) {
                    const glm::ivec2 gCoord(newCenter.x + rx, newCenter.y + rz);
                    const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                    auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
                    cell.Discovered = 0;
                    cell.LiveLit = 0;
                    if (biomeGen) {
                        cell.BiomeColorID = BiomeToColorID(biomeGen->EvaluateBiomeAtXZ(static_cast<float>(gCoord.x) * TexelWorldSize,
                                                                                        static_cast<float>(gCoord.y) * TexelWorldSize).DominantBiome);
                    }
                }
            }
        } else {
            // Scroll X columns
            if (deltaX > 0) {
                for (int gx = oldCenter.x + 128; gx <= newCenter.x + 127; ++gx) {
                    for (int rz = -128; rz < 128; ++rz) {
                        const glm::ivec2 gCoord(gx, newCenter.y + rz);
                        const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                        auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
                        cell.Discovered = 0;
                        cell.LiveLit = 0;
                        if (biomeGen) {
                            cell.BiomeColorID = BiomeToColorID(biomeGen->EvaluateBiomeAtXZ(static_cast<float>(gCoord.x) * TexelWorldSize,
                                                                                            static_cast<float>(gCoord.y) * TexelWorldSize).DominantBiome);
                        }
                    }
                }
            } else if (deltaX < 0) {
                for (int gx = newCenter.x - 128; gx <= oldCenter.x - 129; ++gx) {
                    for (int rz = -128; rz < 128; ++rz) {
                        const glm::ivec2 gCoord(gx, newCenter.y + rz);
                        const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                        auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
                        cell.Discovered = 0;
                        cell.LiveLit = 0;
                        if (biomeGen) {
                            cell.BiomeColorID = BiomeToColorID(biomeGen->EvaluateBiomeAtXZ(static_cast<float>(gCoord.x) * TexelWorldSize,
                                                                                            static_cast<float>(gCoord.y) * TexelWorldSize).DominantBiome);
                        }
                    }
                }
            }

            // Scroll Z rows
            if (deltaZ > 0) {
                for (int gz = oldCenter.y + 128; gz <= newCenter.y + 127; ++gz) {
                    for (int rx = -128; rx < 128; ++rx) {
                        const glm::ivec2 gCoord(newCenter.x + rx, gz);
                        const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                        auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
                        cell.Discovered = 0;
                        cell.LiveLit = 0;
                        if (biomeGen) {
                            cell.BiomeColorID = BiomeToColorID(biomeGen->EvaluateBiomeAtXZ(static_cast<float>(gCoord.x) * TexelWorldSize,
                                                                                            static_cast<float>(gCoord.y) * TexelWorldSize).DominantBiome);
                        }
                    }
                }
            } else if (deltaZ < 0) {
                for (int gz = newCenter.y - 128; gz <= oldCenter.y - 129; ++gz) {
                    for (int rx = -128; rx < 128; ++rx) {
                        const glm::ivec2 gCoord(newCenter.x + rx, gz);
                        const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                        auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
                        cell.Discovered = 0;
                        cell.LiveLit = 0;
                        if (biomeGen) {
                            cell.BiomeColorID = BiomeToColorID(biomeGen->EvaluateBiomeAtXZ(static_cast<float>(gCoord.x) * TexelWorldSize,
                                                                                            static_cast<float>(gCoord.y) * TexelWorldSize).DominantBiome);
                        }
                    }
                }
            }
        }
        m_CurrentGridCenter = newCenter;
    }

    m_CurrentViewerWorldXZ = viewerWorldXZ;

    // 2. Clear live illumination across the active window
    for (int rz = -128; rz < 128; ++rz) {
        for (int rx = -128; rx < 128; ++rx) {
            const glm::ivec2 tCoord = GridToToroidalTexel({ m_CurrentGridCenter.x + rx, m_CurrentGridCenter.y + rz });
            m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x].LiveLit = 0;
        }
    }

    // 3. Stamp Hero Torch live light and permanent discovery
    if (heroTorchRadius > 0.0f) {
        StampExploration(viewerWorldXZ, heroTorchRadius, /*isLiveLit=*/true);
    }

    // 4. Stamp additional light sources (Flare Pegs, Ignited Braziers)
    for (const auto& light : lightSources) {
        // light.x = worldX, light.y = worldZ, light.z = radius
        if (light.z > 0.0f) {
            StampExploration(glm::vec2(light.x, light.y), light.z, /*isLiveLit=*/true);
        }
    }

    // 5. Rebuild display buffer with circular vignette
    RebuildDisplayBuffer();
    m_TextureDirty = true;
}

void MinimapFogSystem::StampExploration(const glm::vec2& worldXZ, float radiusMeters, bool isLiveLit) {
    if (radiusMeters <= 0.0f) return;

    const int radiusTexels = static_cast<int>(std::ceil(radiusMeters / TexelWorldSize));
    const glm::ivec2 centerGrid = WorldToGridCoord(worldXZ);

    for (int dz = -radiusTexels; dz <= radiusTexels; ++dz) {
        for (int dx = -radiusTexels; dx <= radiusTexels; ++dx) {
            const glm::ivec2 gCoord = centerGrid + glm::ivec2(dx, dz);

            // Check if within window around current viewer
            if (std::abs(gCoord.x - m_CurrentGridCenter.x) >= 128 ||
                std::abs(gCoord.y - m_CurrentGridCenter.y) >= 128) {
                continue;
            }

            const glm::vec2 cellWorldPos(static_cast<float>(gCoord.x) * TexelWorldSize + TexelWorldSize * 0.5f,
                                         static_cast<float>(gCoord.y) * TexelWorldSize + TexelWorldSize * 0.5f);
            const float dist = glm::distance(cellWorldPos, worldXZ);

            if (dist <= radiusMeters) {
                const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
                auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];

                // Permanent discovery
                cell.Discovered = kDiscoveredAlphaByte;

                if (isLiveLit) {
                    const float factor = std::clamp(1.0f - (dist / radiusMeters), 0.0f, 1.0f);
                    const uint8_t litVal = static_cast<uint8_t>(factor * 255.0f);
                    cell.LiveLit = std::max(cell.LiveLit, litVal);
                }
            }
        }
    }
}

void MinimapFogSystem::SetBiomeAtWorld(const glm::vec2& worldXZ, BiomeType biome) {
    const glm::ivec2 gCoord = WorldToGridCoord(worldXZ);
    const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
    m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x].BiomeColorID = BiomeToColorID(biome);
    m_TextureDirty = true;
}

glm::u8vec4 MinimapFogSystem::GetTexelAtWorld(const glm::vec2& worldXZ) const noexcept {
    const glm::ivec2 gCoord = WorldToGridCoord(worldXZ);
    return GetTexelAtGrid(gCoord.x, gCoord.y);
}

glm::u8vec4 MinimapFogSystem::GetTexelAtGrid(int gx, int gz) const noexcept {
    const glm::ivec2 tCoord = GridToToroidalTexel({ gx, gz });
    const auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];
    return { cell.BiomeColorID, cell.Discovered, cell.LiveLit, 255 };
}

bool MinimapFogSystem::IsDiscovered(const glm::vec2& worldXZ) const noexcept {
    const glm::ivec2 gCoord = WorldToGridCoord(worldXZ);
    const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
    return m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x].Discovered >= kDiscoveredAlphaByte;
}

float MinimapFogSystem::GetLiveIllumination(const glm::vec2& worldXZ) const noexcept {
    const glm::ivec2 gCoord = WorldToGridCoord(worldXZ);
    const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
    return static_cast<float>(m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x].LiveLit) / 255.0f;
}

glm::vec2 MinimapFogSystem::ProjectAndClampPOIPin(const glm::vec2& poiWorldXZ,
                                                  const glm::vec2& viewerWorldXZ,
                                                  float maxWorldRadius,
                                                  bool& outIsClamped) noexcept {
    const glm::vec2 delta = poiWorldXZ - viewerWorldXZ;
    const float dist = glm::length(delta);

    if (dist <= 1e-4f) {
        outIsClamped = false;
        return glm::vec2(0.0f);
    }

    if (dist <= maxWorldRadius) {
        outIsClamped = false;
        return delta / maxWorldRadius;
    }

    outIsClamped = true;
    return (delta / dist) * 1.0f; // Clamped onto the circular border rim
}

void MinimapFogSystem::AddPin(const MinimapPOIPin& pin) {
    m_ActivePins.push_back(pin);
}

void MinimapFogSystem::RebuildDisplayBuffer() {
    constexpr float kInnerRadius = 120.0f;
    constexpr float kOuterRadius = 126.0f;

    for (uint32_t vy = 0; vy < TextureSize; ++vy) {
        const int dz = static_cast<int>(vy) - 128;
        for (uint32_t vx = 0; vx < TextureSize; ++vx) {
            const int dx = static_cast<int>(vx) - 128;
            const float r = std::sqrt(static_cast<float>(dx * dx + dz * dz));

            float alpha = 0.0f;
            if (r <= kInnerRadius) {
                alpha = 1.0f;
            } else if (r < kOuterRadius) {
                alpha = 1.0f - (r - kInnerRadius) / (kOuterRadius - kInnerRadius);
            }

            const glm::ivec2 gCoord(m_CurrentGridCenter.x + dx, m_CurrentGridCenter.y + dz);
            const glm::ivec2 tCoord = GridToToroidalTexel(gCoord);
            const auto& cell = m_ToroidalGrid[tCoord.y * TextureSize + tCoord.x];

            const size_t outIdx = (static_cast<size_t>(vy) * TextureSize + vx) * 4;
            m_DisplayBuffer[outIdx + 0] = cell.BiomeColorID;
            m_DisplayBuffer[outIdx + 1] = cell.Discovered;
            m_DisplayBuffer[outIdx + 2] = cell.LiveLit;
            m_DisplayBuffer[outIdx + 3] = static_cast<uint8_t>(alpha * 255.0f);
        }
    }
}

void MinimapFogSystem::SyncGpuTexture() {
    if (m_FogTexture && m_FogTexture->GetRendererID() != 0 && m_TextureDirty) {
        m_FogTexture->SetData(m_DisplayBuffer.data(), static_cast<uint32_t>(m_DisplayBuffer.size()));
        m_TextureDirty = false;
    }
}

void MinimapFogSystem::UpdateFromScene(const Scene& scene, const glm::vec2& viewerWorldXZ) {
    std::vector<glm::vec3> lights;
    m_ActivePins.clear();

    // 1. Gather live lights and POIs from scene entities
    float heroTorchRadius = 0.0f;

    for (const auto& go : scene.GetGameObjects()) {
        if (!go || !go->IsActive()) continue;

        const auto* pointLight = go->GetComponent<PointLightComponent>();
        const glm::vec3 worldPos = go->GetTransform() ? go->GetTransform()->GetWorldPosition() : glm::vec3(0.0f);
        const glm::vec2 worldXZ(worldPos.x, worldPos.z);

        if (pointLight && pointLight->Intensity > 0.0f) {
            if (pointLight->Priority == LightPriority::Critical_HeroTorch) {
                heroTorchRadius = pointLight->Radius;
            } else if (pointLight->Semantic == LightSemantic::SafeLifeline) {
                lights.emplace_back(worldXZ.x, worldXZ.y, pointLight->Radius);
            }
        }

        // Register POI pins
        const std::string& name = go->GetName();
        bool isIgnited = (pointLight && pointLight->Intensity > 0.1f && pointLight->Semantic == LightSemantic::SafeLifeline);

        if (name.find("Brazier") != std::string::npos || name.find("brazier") != std::string::npos) {
            MinimapPOIPin pin{};
            pin.EntityUUID = go->GetUUID();
            pin.Type = isIgnited ? POIType::IgnitedBrazier : POIType::DormantBrazier;
            pin.WorldXZ = worldXZ;
            pin.Label = name;
            pin.IconColor = isIgnited ? glm::vec4(1.0f, 0.75f, 0.2f, 1.0f) : glm::vec4(0.45f, 0.45f, 0.5f, 0.8f);
            pin.MinimapLocalPos = ProjectAndClampPOIPin(worldXZ, viewerWorldXZ, WorldRadius, pin.IsClampedToRim);
            AddPin(pin);
        } else if (name.find("Shrine") != std::string::npos || name.find("shrine") != std::string::npos) {
            MinimapPOIPin pin{};
            pin.EntityUUID = go->GetUUID();
            pin.Type = POIType::UmbralShrine;
            pin.WorldXZ = worldXZ;
            pin.Label = name;
            pin.IconColor = glm::vec4(0.3f, 0.85f, 0.95f, 1.0f);
            pin.MinimapLocalPos = ProjectAndClampPOIPin(worldXZ, viewerWorldXZ, WorldRadius, pin.IsClampedToRim);
            AddPin(pin);
        }
    }

    Update(viewerWorldXZ, heroTorchRadius, lights, nullptr);
    SyncGpuTexture();
}

} // namespace Engine
