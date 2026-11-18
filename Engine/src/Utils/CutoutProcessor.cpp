#include "Engine/Utils/CutoutProcessor.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Utils/Logger.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace Engine {

namespace {

glm::vec4 SampleCornerAverageRGBA(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
    const uint32_t patchW = std::min<uint32_t>(4u, width);
    const uint32_t patchH = std::min<uint32_t>(4u, height);
    const uint32_t cornerX[2] = { 0u, width - patchW };
    const uint32_t cornerY[2] = { 0u, height - patchH };

    glm::vec4 accum{ 0.0f };
    float count = 0.0f;

    for (uint32_t cy : cornerY) {
        for (uint32_t cx : cornerX) {
            for (uint32_t dy = 0; dy < patchH; ++dy) {
                for (uint32_t dx = 0; dx < patchW; ++dx) {
                    const size_t idx = (static_cast<size_t>(cy + dy) * width + (cx + dx)) * 4u;
                    accum.r += static_cast<float>(rgba[idx + 0]) / 255.0f;
                    accum.g += static_cast<float>(rgba[idx + 1]) / 255.0f;
                    accum.b += static_cast<float>(rgba[idx + 2]) / 255.0f;
                    accum.a += static_cast<float>(rgba[idx + 3]) / 255.0f;
                    count += 1.0f;
                }
            }
        }
    }

    return (count > 0.0f) ? (accum / count) : glm::vec4(0.0f);
}

float SmoothStep01(float edge0, float edge1, float x) {
    const float denom = std::max(edge1 - edge0, 1e-5f);
    const float t = std::clamp((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void ApplyChromaKeyAndDespill(std::vector<uint8_t>& rgba,
                              uint32_t width,
                              uint32_t height,
                              const CutoutProcessSettings& settings,
                              CutoutKeyMode& outResolvedMode,
                              glm::vec3& outKeyColor) {
    const glm::vec4 cornerAvg = SampleCornerAverageRGBA(rgba, width, height);
    CutoutKeyMode activeMode = settings.BackgroundMode;
    glm::vec3 keyColor = { cornerAvg.r, cornerAvg.g, cornerAvg.b };
    bool useSolidColorFallback = false;

    if (activeMode == CutoutKeyMode::AutoDetectCorner) {
        if (cornerAvg.a < 0.08f) {
            activeMode = CutoutKeyMode::PreserveExistingAlpha;
        } else if (cornerAvg.g > 0.62f && (cornerAvg.g - std::max(cornerAvg.r, cornerAvg.b)) > 0.32f) {
            activeMode = CutoutKeyMode::ChromaGreen;
        } else if (cornerAvg.r > 0.62f && cornerAvg.b > 0.62f &&
                   (std::min(cornerAvg.r, cornerAvg.b) - cornerAvg.g) > 0.32f) {
            activeMode = CutoutKeyMode::ChromaMagenta;
        } else {
            useSolidColorFallback = true;
        }
    } else if (activeMode == CutoutKeyMode::ChromaGreen) {
        if (cornerAvg.g <= 0.5f) {
            keyColor = { 0.0f, 1.0f, 0.0f };
        }
    } else if (activeMode == CutoutKeyMode::ChromaMagenta) {
        if (cornerAvg.r <= 0.5f || cornerAvg.b <= 0.5f) {
            keyColor = { 1.0f, 0.0f, 1.0f };
        }
    }
    outResolvedMode = activeMode;
    outKeyColor = keyColor;

    if (activeMode == CutoutKeyMode::PreserveExistingAlpha) {
        return;
    }

    const float tol = std::clamp(settings.ChromaTolerance, 0.02f, 1.20f);
    const float soft = std::clamp(settings.ChromaSoftness, 0.01f, 0.60f);
    const size_t pixelCount = static_cast<size_t>(width) * height;

    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t idx = i * 4u;
        glm::vec3 rgb = {
            static_cast<float>(rgba[idx + 0]) / 255.0f,
            static_cast<float>(rgba[idx + 1]) / 255.0f,
            static_cast<float>(rgba[idx + 2]) / 255.0f
        };
        const float srcAlpha = static_cast<float>(rgba[idx + 3]) / 255.0f;

        float keyAlpha = 1.0f;
        if (activeMode == CutoutKeyMode::ChromaGreen) {
            const float distToKey = glm::length(rgb - keyColor);
            const float maxRB = std::max(rgb.r, rgb.b);
            const float greenDominance = rgb.g - maxRB;

            const float distAlpha = SmoothStep01(tol, tol + soft, distToKey);
            const float domAlpha  = (greenDominance > 0.18f)
                ? (1.0f - SmoothStep01(0.18f, 0.58f, greenDominance))
                : 1.0f;
            keyAlpha = std::min(distAlpha, domAlpha);

            if (settings.DespillFringe && greenDominance > 0.02f && keyAlpha < 0.99f) {
                const float spillStrength = 1.0f - keyAlpha;
                rgb.g = glm::mix(rgb.g, maxRB, std::min(1.0f, spillStrength * 1.15f));
            }
        } else if (activeMode == CutoutKeyMode::ChromaMagenta) {
            const float distToKey = glm::length(rgb - keyColor);
            const float magentaDominance = std::min(rgb.r, rgb.b) - rgb.g;

            const float distAlpha = SmoothStep01(tol, tol + soft, distToKey);
            const float domAlpha  = (magentaDominance > 0.18f)
                ? (1.0f - SmoothStep01(0.18f, 0.58f, magentaDominance))
                : 1.0f;
            keyAlpha = std::min(distAlpha, domAlpha);

            if (settings.DespillFringe && magentaDominance > 0.02f && keyAlpha < 0.99f) {
                const float spillStrength = std::min(1.0f, (1.0f - keyAlpha) * 1.15f);
                rgb.r = glm::mix(rgb.r, std::min(rgb.r, rgb.g + 0.04f), spillStrength);
                rgb.b = glm::mix(rgb.b, std::min(rgb.b, rgb.g + 0.04f), spillStrength);
            }
        } else if (useSolidColorFallback) {
            const float distToKey = glm::length(rgb - keyColor);
            keyAlpha = SmoothStep01(tol * 0.65f, tol * 0.65f + soft, distToKey);
        }

        const float finalAlpha = (keyAlpha < 0.04f) ? 0.0f : (srcAlpha * keyAlpha);
        rgba[idx + 0] = static_cast<uint8_t>(std::clamp(std::round(rgb.r * 255.0f), 0.0f, 255.0f));
        rgba[idx + 1] = static_cast<uint8_t>(std::clamp(std::round(rgb.g * 255.0f), 0.0f, 255.0f));
        rgba[idx + 2] = static_cast<uint8_t>(std::clamp(std::round(rgb.b * 255.0f), 0.0f, 255.0f));
        rgba[idx + 3] = static_cast<uint8_t>(std::clamp(std::round(finalAlpha * 255.0f), 0.0f, 255.0f));
    }

    // Remove isolated 1-pixel JPEG compression ringing specks in transparent regions
    if (width >= 3 && height >= 3) {
        std::vector<uint8_t> alphaCopy(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < alphaCopy.size(); ++i) {
            alphaCopy[i] = rgba[i * 4u + 3];
        }
        for (uint32_t y = 1; y + 1 < height; ++y) {
            for (uint32_t x = 1; x + 1 < width; ++x) {
                const size_t p = static_cast<size_t>(y) * width + x;
                if (alphaCopy[p] > 0 && alphaCopy[p] < 110) {
                    uint8_t maxNeighborA = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const size_t np = static_cast<size_t>(static_cast<int>(y) + dy) * width +
                                              static_cast<size_t>(static_cast<int>(x) + dx);
                            maxNeighborA = std::max(maxNeighborA, alphaCopy[np]);
                        }
                    }
                    if (maxNeighborA < settings.TrimAlphaThreshold) {
                        rgba[p * 4u + 3] = 0;
                    }
                }
            }
        }
    }
}

void ApplyAlphaBleedDilation(std::vector<uint8_t>& rgba,
                             uint32_t width,
                             uint32_t height,
                             uint32_t passes,
                             uint8_t alphaThreshold) {
    if (passes == 0 || width < 2 || height < 2) {
        return;
    }

    const size_t totalPixels = static_cast<size_t>(width) * height;
    std::vector<uint8_t> validRGBMask(totalPixels, 0u);
    for (size_t i = 0; i < totalPixels; ++i) {
        if (rgba[i * 4u + 3] >= alphaThreshold) {
            validRGBMask[i] = 1u;
        }
    }

    std::vector<uint8_t> prevRGBA = rgba;
    std::vector<size_t> dilatedIndices;
    dilatedIndices.reserve(totalPixels / 8);

    for (uint32_t pass = 0; pass < passes; ++pass) {
        dilatedIndices.clear();

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t center = static_cast<size_t>(y) * width + x;
                if (validRGBMask[center] != 0u) {
                    continue;
                }

                float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, weightSum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int ny = static_cast<int>(y) + dy;
                    if (ny < 0 || ny >= static_cast<int>(height)) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = static_cast<int>(x) + dx;
                        if (nx < 0 || nx >= static_cast<int>(width)) continue;

                        const size_t nIdx = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                        if (validRGBMask[nIdx] != 0u) {
                            const float w = (dx == 0 || dy == 0) ? 1.0f : 0.707f;
                            sumR += static_cast<float>(prevRGBA[nIdx * 4u + 0]) * w;
                            sumG += static_cast<float>(prevRGBA[nIdx * 4u + 1]) * w;
                            sumB += static_cast<float>(prevRGBA[nIdx * 4u + 2]) * w;
                            weightSum += w;
                        }
                    }
                }

                if (weightSum > 0.0f) {
                    rgba[center * 4u + 0] = static_cast<uint8_t>(std::clamp(std::round(sumR / weightSum), 0.0f, 255.0f));
                    rgba[center * 4u + 1] = static_cast<uint8_t>(std::clamp(std::round(sumG / weightSum), 0.0f, 255.0f));
                    rgba[center * 4u + 2] = static_cast<uint8_t>(std::clamp(std::round(sumB / weightSum), 0.0f, 255.0f));
                    dilatedIndices.push_back(center);
                }
            }
        }

        if (dilatedIndices.empty()) {
            break;
        }

        for (size_t idx : dilatedIndices) {
            validRGBMask[idx] = 1u;
            prevRGBA[idx * 4u + 0] = rgba[idx * 4u + 0];
            prevRGBA[idx * 4u + 1] = rgba[idx * 4u + 1];
            prevRGBA[idx * 4u + 2] = rgba[idx * 4u + 2];
        }
    }
}

std::mutex s_StbWriteMutex;

} // namespace

CutoutProcessResult CutoutProcessor::ProcessRGBAInPlace(std::vector<uint8_t>& rgba,
                                                        uint32_t& width,
                                                        uint32_t& height,
                                                        const CutoutProcessSettings& settings) {
    CutoutProcessResult result;
    if (width == 0 || height == 0 || rgba.size() < static_cast<size_t>(width) * height * 4u) {
        result.ErrorMessage = "Invalid or empty RGBA image buffer";
        return result;
    }

    // 1 & 2. Apply Chroma Keying and Anti-Aliased Fringe Despill
    CutoutKeyMode activeMode = CutoutKeyMode::PreserveExistingAlpha;
    glm::vec3 keyColor{ 0.0f };
    ApplyChromaKeyAndDespill(rgba, width, height, settings, activeMode, keyColor);
    result.ResolvedKeyMode = activeMode;
    ENGINE_CORE_DEBUG("CutoutProcessor: Key mode resolved to {} (Key RGB=[{:.2f}, {:.2f}, {:.2f}])",
                      static_cast<int>(activeMode), keyColor.r, keyColor.g, keyColor.b);

    // 3. Measure solid silhouette bounds & optionally Auto-Trim + Bottom-Flush align to UV.y = 0
    uint32_t minX = width, minY = height;
    uint32_t maxX = 0, maxY = 0;
    bool foundOpaque = false;
    const uint8_t alphaThreshold = std::max<uint8_t>(1u, settings.TrimAlphaThreshold);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t a = rgba[(static_cast<size_t>(y) * width + x) * 4u + 3];
            if (a >= alphaThreshold) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                foundOpaque = true;
            }
        }
    }

    if (!foundOpaque) {
        result.ErrorMessage = "No opaque silhouette pixels remained after chroma keying";
        return result;
    }

    result.ContentWidth = maxX - minX + 1u;
    result.ContentHeight = maxY - minY + 1u;
    result.SuggestedAspectRatio = static_cast<float>(result.ContentWidth) /
                                  static_cast<float>(std::max(1u, result.ContentHeight));
    ENGINE_CORE_DEBUG("CutoutProcessor: Silhouette bounds [{}, {}] - [{}, {}] -> content size {}x{} (aspect {:.3f})",
                      minX, minY, maxX, maxY, result.ContentWidth, result.ContentHeight, result.SuggestedAspectRatio);

    if (settings.AutoTrimAndBottomFlush) {
        uint32_t outW = result.ContentWidth + 2u * settings.HorizontalPaddingPx;
        uint32_t outH = result.ContentHeight + settings.TopPaddingPx;
        if (settings.SquareCanvas) {
            const uint32_t side = std::max(outW, outH);
            outW = side;
            outH = side;
        }

        const uint32_t dstStartX = (outW - result.ContentWidth) / 2u;
        // Bottom-flush: lowest opaque row (maxY) lands directly on row (outH - 1), which maps to UV.y = 0.0!
        const uint32_t dstStartY = outH - result.ContentHeight;

        std::vector<uint8_t> cropped(static_cast<size_t>(outW) * outH * 4u, 0u);
        for (uint32_t cy = 0; cy < result.ContentHeight; ++cy) {
            const uint32_t srcY = minY + cy;
            const uint32_t dstY = dstStartY + cy;
            for (uint32_t cx = 0; cx < result.ContentWidth; ++cx) {
                const uint32_t srcX = minX + cx;
                const uint32_t dstX = dstStartX + cx;
                const size_t srcIdx = (static_cast<size_t>(srcY) * width + srcX) * 4u;
                const size_t dstIdx = (static_cast<size_t>(dstY) * outW + dstX) * 4u;
                cropped[dstIdx + 0] = rgba[srcIdx + 0];
                cropped[dstIdx + 1] = rgba[srcIdx + 1];
                cropped[dstIdx + 2] = rgba[srcIdx + 2];
                cropped[dstIdx + 3] = rgba[srcIdx + 3];
            }
        }

        rgba.swap(cropped);
        width = outW;
        height = outH;
        ENGINE_CORE_DEBUG("CutoutProcessor: Bottom-flushed and trimmed canvas to {}x{} (dstStartX={}, dstStartY={})",
                          outW, outH, dstStartX, dstStartY);
    }

    // 4. Multi-Pass Alpha-Edge RGB Dilation (Color Bleed into A == 0 pixels to eliminate mipmap halos)
    if (settings.AlphaBleedPasses > 0 && width >= 2 && height >= 2) {
        ApplyAlphaBleedDilation(rgba, width, height, settings.AlphaBleedPasses, alphaThreshold);
        ENGINE_CORE_DEBUG("CutoutProcessor: Completed {} alpha bleed dilation passes", settings.AlphaBleedPasses);
    }

    result.Success = true;
    result.OutputWidth = width;
    result.OutputHeight = height;
    return result;
}

CutoutProcessResult CutoutProcessor::ProcessFile(const std::filesystem::path& inputPath,
                                                 const std::filesystem::path& outputPath,
                                                 const CutoutProcessSettings& settings) {
    CutoutProcessResult result;
    const std::string inStr = inputPath.string();
    const std::string outStr = outputPath.string();

    ENGINE_CORE_INFO("CutoutProcessor: Starting processing of '{}'...", inStr);

    // Load in top-down PNG row order (row 0 = top, row H-1 = bottom ground contact)
    stbi_set_flip_vertically_on_load_thread(0);
    int w = 0, h = 0, channels = 0;
    unsigned char* rawPixels = stbi_load(inStr.c_str(), &w, &h, &channels, 4);

    if (!rawPixels || w <= 0 || h <= 0) {
        if (rawPixels) {
            stbi_image_free(rawPixels);
        }
        result.ErrorMessage = "Failed to load source image: " + inStr;
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessFile failed: {}", result.ErrorMessage);
        return result;
    }

    uint32_t width = static_cast<uint32_t>(w);
    uint32_t height = static_cast<uint32_t>(h);
    std::vector<uint8_t> rgba(rawPixels, rawPixels + static_cast<size_t>(width) * height * 4u);
    stbi_image_free(rawPixels);

    result = ProcessRGBAInPlace(rgba, width, height, settings);
    if (!result.Success) {
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessFile failed for '{}': {}", inStr, result.ErrorMessage);
        return result;
    }

    std::error_code ec;
    const auto parentDir = outputPath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
    }

    int writeOk = 0;
    {
        std::lock_guard<std::mutex> writeLock(s_StbWriteMutex);
        stbi_flip_vertically_on_write(0);
        writeOk = stbi_write_png(outStr.c_str(),
                                 static_cast<int>(width),
                                 static_cast<int>(height),
                                 4,
                                 rgba.data(),
                                 static_cast<int>(width * 4u));
    }
    if (writeOk == 0) {
        result.Success = false;
        result.ErrorMessage = "Failed to write processed PNG to: " + outStr;
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessFile failed: {}", result.ErrorMessage);
        return result;
    }

    ENGINE_CORE_INFO("CutoutProcessor: Processed '{}' -> '{}' ({}x{}, aspect {:.3f})",
                     inStr, outStr, result.OutputWidth, result.OutputHeight, result.SuggestedAspectRatio);
    return result;
}

JobHandle CutoutProcessor::ProcessFileAsync(const std::filesystem::path& inputPath,
                                            const std::filesystem::path& outputPath,
                                            const CutoutProcessSettings& settings,
                                            std::function<void(const CutoutProcessResult&)> onComplete) {
    if (!JobSystem::IsInitialized() || JobSystem::GetWorkerCount() == 0) {
        CutoutProcessResult result = ProcessFile(inputPath, outputPath, settings);
        if (onComplete) {
            onComplete(result);
        }
        return JobHandle{};
    }

    return JobSystem::Dispatch([inputPath, outputPath, settings, onComplete = std::move(onComplete)]() {
        CutoutProcessResult result = ProcessFile(inputPath, outputPath, settings);
        if (onComplete) {
            onComplete(result);
        }
    });
}

CutoutProcessResult CutoutProcessor::ProcessAnimationStrip(
    const std::vector<std::filesystem::path>& inputFrameFiles,
    const std::filesystem::path& outputSheetPng,
    const std::filesystem::path& outputAtlasJson,
    const SheetCutoutSettings& settings) {
    CutoutProcessResult result;

    if (inputFrameFiles.empty()) {
        result.ErrorMessage = "No input frame files provided";
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
        return result;
    }

    struct FrameItem {
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };

    std::vector<FrameItem> frames;

    const bool isGridSlice = (inputFrameFiles.size() == 1u) && (settings.Columns > 1u || settings.Rows > 1u);

    if (isGridSlice) {
        const std::string inStr = inputFrameFiles[0].string();
        stbi_set_flip_vertically_on_load_thread(0);
        int w = 0, h = 0, channels = 0;
        unsigned char* rawPixels = stbi_load(inStr.c_str(), &w, &h, &channels, 4);
        if (!rawPixels || w <= 0 || h <= 0) {
            if (rawPixels) stbi_image_free(rawPixels);
            result.ErrorMessage = "Failed to load grid source image: " + inStr;
            ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
            return result;
        }

        const uint32_t srcW = static_cast<uint32_t>(w);
        const uint32_t srcH = static_cast<uint32_t>(h);
        const uint32_t gridCols = std::max(1u, settings.Columns);
        const uint32_t gridRows = std::max(1u, settings.Rows);
        const uint32_t cellW = srcW / gridCols;
        const uint32_t cellH = srcH / gridRows;

        if (cellW == 0 || cellH == 0) {
            stbi_image_free(rawPixels);
            result.ErrorMessage = "Grid slicing dimensions invalid: cell size is zero";
            ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
            return result;
        }

        const std::string stem = settings.FrameNamePrefix.empty()
            ? inputFrameFiles[0].stem().string()
            : settings.FrameNamePrefix;

        uint32_t frameIdx = 0;
        for (uint32_t r = 0; r < gridRows; ++r) {
            for (uint32_t c = 0; c < gridCols; ++c) {
                FrameItem item;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s_%02u", stem.c_str(), frameIdx++);
                item.name = buf;
                item.width = cellW;
                item.height = cellH;
                item.rgba.resize(static_cast<size_t>(cellW) * cellH * 4u);

                const uint32_t originX = c * cellW;
                const uint32_t originY = r * cellH;

                for (uint32_t y = 0; y < cellH; ++y) {
                    const size_t srcRowOffset = (static_cast<size_t>(originY + y) * srcW + originX) * 4u;
                    const size_t dstRowOffset = (static_cast<size_t>(y) * cellW) * 4u;
                    std::memcpy(&item.rgba[dstRowOffset], &rawPixels[srcRowOffset], static_cast<size_t>(cellW) * 4u);
                }

                frames.push_back(std::move(item));
            }
        }
        stbi_image_free(rawPixels);
    } else {
        frames.reserve(inputFrameFiles.size());
        uint32_t frameIdx = 0;
        for (const auto& filePath : inputFrameFiles) {
            const std::string inStr = filePath.string();
            stbi_set_flip_vertically_on_load_thread(0);
            int w = 0, h = 0, channels = 0;
            unsigned char* rawPixels = stbi_load(inStr.c_str(), &w, &h, &channels, 4);
            if (!rawPixels || w <= 0 || h <= 0) {
                if (rawPixels) stbi_image_free(rawPixels);
                result.ErrorMessage = "Failed to load frame image: " + inStr;
                ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
                return result;
            }

            FrameItem item;
            if (!settings.FrameNamePrefix.empty()) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s_%02u", settings.FrameNamePrefix.c_str(), frameIdx++);
                item.name = buf;
            } else {
                item.name = filePath.stem().string();
            }
            item.width = static_cast<uint32_t>(w);
            item.height = static_cast<uint32_t>(h);
            item.rgba.assign(rawPixels, rawPixels + static_cast<size_t>(item.width) * item.height * 4u);
            stbi_image_free(rawPixels);

            frames.push_back(std::move(item));
        }
    }

    if (frames.empty()) {
        result.ErrorMessage = "No frames extracted";
        return result;
    }

    // Key and despill each frame
    CutoutKeyMode primaryKeyMode = CutoutKeyMode::PreserveExistingAlpha;
    for (size_t i = 0; i < frames.size(); ++i) {
        CutoutKeyMode mode = CutoutKeyMode::PreserveExistingAlpha;
        glm::vec3 keyColor{ 0.0f };
        ApplyChromaKeyAndDespill(frames[i].rgba, frames[i].width, frames[i].height, settings.BaseSettings, mode, keyColor);
        if (i == 0) {
            primaryKeyMode = mode;
        }
    }
    result.ResolvedKeyMode = primaryKeyMode;

    // Detect silhouette bounds across all frames
    const uint8_t alphaThreshold = std::max<uint8_t>(1u, settings.BaseSettings.TrimAlphaThreshold);
    uint32_t unionMinX = UINT32_MAX, unionMinY = UINT32_MAX;
    uint32_t unionMaxX = 0, unionMaxY = 0;
    bool foundAnyOpaque = false;

    struct FrameBounds {
        uint32_t minX = UINT32_MAX, minY = UINT32_MAX;
        uint32_t maxX = 0, maxY = 0;
        bool hasOpaque = false;
    };
    std::vector<FrameBounds> perFrameBounds(frames.size());

    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& fr = frames[i];
        auto& fb = perFrameBounds[i];
        for (uint32_t y = 0; y < fr.height; ++y) {
            for (uint32_t x = 0; x < fr.width; ++x) {
                const uint8_t a = fr.rgba[(static_cast<size_t>(y) * fr.width + x) * 4u + 3];
                if (a >= alphaThreshold) {
                    fb.minX = std::min(fb.minX, x);
                    fb.minY = std::min(fb.minY, y);
                    fb.maxX = std::max(fb.maxX, x);
                    fb.maxY = std::max(fb.maxY, y);
                    fb.hasOpaque = true;
                }
            }
        }
        if (fb.hasOpaque) {
            unionMinX = std::min(unionMinX, fb.minX);
            unionMinY = std::min(unionMinY, fb.minY);
            unionMaxX = std::max(unionMaxX, fb.maxX);
            unionMaxY = std::max(unionMaxY, fb.maxY);
            foundAnyOpaque = true;
        }
    }

    if (!foundAnyOpaque) {
        result.ErrorMessage = "No opaque silhouette pixels remained in animation sequence";
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
        return result;
    }

    const uint32_t unionContentW = unionMaxX - unionMinX + 1u;
    const uint32_t unionContentH = unionMaxY - unionMinY + 1u;
    result.ContentWidth = unionContentW;
    result.ContentHeight = unionContentH;
    result.SuggestedAspectRatio = static_cast<float>(unionContentW) / static_cast<float>(std::max(1u, unionContentH));

    // Standardized cell size with padding
    uint32_t cellW = unionContentW + 2u * settings.BaseSettings.HorizontalPaddingPx;
    uint32_t cellH = unionContentH + settings.BaseSettings.TopPaddingPx +
                     (settings.LockBottomFootPivot ? 0u : settings.BaseSettings.HorizontalPaddingPx);

    if (settings.BaseSettings.SquareCanvas) {
        const uint32_t side = std::max(cellW, cellH);
        cellW = side;
        cellH = side;
    }

    const uint32_t dstStartX = settings.LockHorizontalCenter ? ((cellW - unionContentW) / 2u)
                                                             : settings.BaseSettings.HorizontalPaddingPx;
    const uint32_t dstStartY = settings.LockBottomFootPivot ? (cellH - unionContentH)
                                                           : settings.BaseSettings.TopPaddingPx;

    const size_t numFrames = frames.size();
    const uint32_t outCols = (settings.Columns > 0u) ? settings.Columns : static_cast<uint32_t>(numFrames);
    const uint32_t outRows = (static_cast<uint32_t>(numFrames) + outCols - 1u) / outCols;
    const uint32_t sheetW = outCols * cellW;
    const uint32_t sheetH = outRows * cellH;

    std::vector<uint8_t> sheetRGBA(static_cast<size_t>(sheetW) * sheetH * 4u, 0u);

    for (size_t i = 0; i < numFrames; ++i) {
        const auto& fr = frames[i];
        const uint32_t col = static_cast<uint32_t>(i) % outCols;
        const uint32_t row = static_cast<uint32_t>(i) / outCols;
        const uint32_t cellOriginX = col * cellW;
        const uint32_t cellOriginY = row * cellH;

        if (settings.LockSharedUnionBounds) {
            for (uint32_t cy = 0; cy < unionContentH; ++cy) {
                const uint32_t srcY = unionMinY + cy;
                if (srcY >= fr.height) continue;
                const uint32_t dstY = cellOriginY + dstStartY + cy;
                for (uint32_t cx = 0; cx < unionContentW; ++cx) {
                    const uint32_t srcX = unionMinX + cx;
                    if (srcX >= fr.width) continue;
                    const uint32_t dstX = cellOriginX + dstStartX + cx;

                    const size_t srcIdx = (static_cast<size_t>(srcY) * fr.width + srcX) * 4u;
                    const size_t dstIdx = (static_cast<size_t>(dstY) * sheetW + dstX) * 4u;
                    sheetRGBA[dstIdx + 0] = fr.rgba[srcIdx + 0];
                    sheetRGBA[dstIdx + 1] = fr.rgba[srcIdx + 1];
                    sheetRGBA[dstIdx + 2] = fr.rgba[srcIdx + 2];
                    sheetRGBA[dstIdx + 3] = fr.rgba[srcIdx + 3];
                }
            }
        } else {
            const auto& fb = perFrameBounds[i];
            if (fb.hasOpaque) {
                const uint32_t localW = fb.maxX - fb.minX + 1u;
                const uint32_t localH = fb.maxY - fb.minY + 1u;
                const uint32_t localDstX = (cellW > localW) ? (cellW - localW) / 2u : 0u;
                const uint32_t localDstY = (cellH > localH) ? cellH - localH : 0u;
                for (uint32_t cy = 0; cy < localH; ++cy) {
                    const uint32_t srcY = fb.minY + cy;
                    if (srcY >= fr.height) continue;
                    const uint32_t dstY = cellOriginY + localDstY + cy;
                    for (uint32_t cx = 0; cx < localW; ++cx) {
                        const uint32_t srcX = fb.minX + cx;
                        if (srcX >= fr.width) continue;
                        const uint32_t dstX = cellOriginX + localDstX + cx;

                        const size_t srcIdx = (static_cast<size_t>(srcY) * fr.width + srcX) * 4u;
                        const size_t dstIdx = (static_cast<size_t>(dstY) * sheetW + dstX) * 4u;
                        sheetRGBA[dstIdx + 0] = fr.rgba[srcIdx + 0];
                        sheetRGBA[dstIdx + 1] = fr.rgba[srcIdx + 1];
                        sheetRGBA[dstIdx + 2] = fr.rgba[srcIdx + 2];
                        sheetRGBA[dstIdx + 3] = fr.rgba[srcIdx + 3];
                    }
                }
            }
        }
    }

    // Alpha bleed dilation across composite sheet
    if (settings.BaseSettings.AlphaBleedPasses > 0u && sheetW >= 2u && sheetH >= 2u) {
        ApplyAlphaBleedDilation(sheetRGBA, sheetW, sheetH, settings.BaseSettings.AlphaBleedPasses, alphaThreshold);
        ENGINE_CORE_DEBUG("CutoutProcessor: Dilation applied across composite sheet ({} passes)",
                          settings.BaseSettings.AlphaBleedPasses);
    }

    // Write output sheet PNG
    std::error_code ec;
    const auto parentPng = outputSheetPng.parent_path();
    if (!parentPng.empty() && !std::filesystem::exists(parentPng, ec)) {
        std::filesystem::create_directories(parentPng, ec);
    }

    int writeOk = 0;
    {
        std::lock_guard<std::mutex> writeLock(s_StbWriteMutex);
        stbi_flip_vertically_on_write(0);
        writeOk = stbi_write_png(outputSheetPng.string().c_str(),
                                 static_cast<int>(sheetW),
                                 static_cast<int>(sheetH),
                                 4,
                                 sheetRGBA.data(),
                                 static_cast<int>(sheetW * 4u));
    }
    if (writeOk == 0) {
        result.ErrorMessage = "Failed to write sheet PNG to: " + outputSheetPng.string();
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
        return result;
    }

    // Generate TexturePacker JSON Hash metadata
    nlohmann::json root;
    for (size_t i = 0; i < numFrames; ++i) {
        const uint32_t col = static_cast<uint32_t>(i) % outCols;
        const uint32_t row = static_cast<uint32_t>(i) / outCols;
        const uint32_t cellOriginX = col * cellW;
        const uint32_t cellOriginY = row * cellH;

        nlohmann::json f;
        f["frame"] = {
            { "x", static_cast<float>(cellOriginX) },
            { "y", static_cast<float>(cellOriginY) },
            { "w", static_cast<float>(cellW) },
            { "h", static_cast<float>(cellH) }
        };
        f["rotated"] = false;
        f["trimmed"] = false;
        f["spriteSourceSize"] = {
            { "x", 0.0f },
            { "y", 0.0f },
            { "w", static_cast<float>(cellW) },
            { "h", static_cast<float>(cellH) }
        };
        f["sourceSize"] = {
            { "w", static_cast<float>(cellW) },
            { "h", static_cast<float>(cellH) }
        };
        f["pivot"] = {
            { "x", 0.5f },
            { "y", 0.0f }
        };
        f["duration"] = settings.FrameDurationMs;

        root["frames"][frames[i].name] = std::move(f);
    }

    std::string relImageName = outputSheetPng.filename().string();
    auto rel = std::filesystem::relative(outputSheetPng, outputAtlasJson.parent_path(), ec);
    if (!ec && !rel.empty()) {
        relImageName = rel.generic_string();
    }

    root["meta"] = {
        { "app", "AntigravityCutoutProcessor" },
        { "version", "1.0" },
        { "image", relImageName },
        { "format", "RGBA8888" },
        { "size", { { "w", static_cast<float>(sheetW) }, { "h", static_cast<float>(sheetH) } } },
        { "scale", "1" }
    };

    const auto parentJson = outputAtlasJson.parent_path();
    if (!parentJson.empty() && !std::filesystem::exists(parentJson, ec)) {
        std::filesystem::create_directories(parentJson, ec);
    }

    std::ofstream jsonFile(outputAtlasJson);
    if (!jsonFile.is_open()) {
        result.ErrorMessage = "Failed to write atlas JSON to: " + outputAtlasJson.string();
        ENGINE_CORE_ERROR("CutoutProcessor::ProcessAnimationStrip failed: {}", result.ErrorMessage);
        return result;
    }
    jsonFile << root.dump(2) << std::endl;
    jsonFile.close();

    result.Success = true;
    result.OutputWidth = sheetW;
    result.OutputHeight = sheetH;
    ENGINE_CORE_INFO("CutoutProcessor: Processed {} frames into sheet '{}' ({}x{}) and atlas '{}'",
                     numFrames, outputSheetPng.string(), sheetW, sheetH, outputAtlasJson.string());
    return result;
}

JobHandle CutoutProcessor::ProcessAnimationStripAsync(
    const std::vector<std::filesystem::path>& inputFrameFiles,
    const std::filesystem::path& outputSheetPng,
    const std::filesystem::path& outputAtlasJson,
    const SheetCutoutSettings& settings,
    std::function<void(const CutoutProcessResult&)> onComplete) {
    if (!JobSystem::IsInitialized() || JobSystem::GetWorkerCount() == 0) {
        CutoutProcessResult result = ProcessAnimationStrip(inputFrameFiles, outputSheetPng, outputAtlasJson, settings);
        if (onComplete) {
            onComplete(result);
        }
        return JobHandle{};
    }

    return JobSystem::Dispatch([inputFrameFiles, outputSheetPng, outputAtlasJson, settings, onComplete = std::move(onComplete)]() {
        CutoutProcessResult result = ProcessAnimationStrip(inputFrameFiles, outputSheetPng, outputAtlasJson, settings);
        if (onComplete) {
            onComplete(result);
        }
    });
}

} // namespace Engine

