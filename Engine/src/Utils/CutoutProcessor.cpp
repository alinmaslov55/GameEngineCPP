#include "Engine/Utils/CutoutProcessor.h"
#include "Engine/Utils/Logger.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
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

    // 1. Detect background key color & mode
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
            // Uniform opaque solid backdrop in corners (e.g. flat white or studio color)
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
    result.ResolvedKeyMode = activeMode;
    ENGINE_CORE_DEBUG("CutoutProcessor: Key mode resolved to {} (Key RGB=[{:.2f}, {:.2f}, {:.2f}], cornerAlpha={:.2f})",
                      static_cast<int>(activeMode), keyColor.r, keyColor.g, keyColor.b, cornerAvg.a);

    // 2. Global Chroma Keying + Anti-Aliased Fringe Despill (handles interior holes cleanly)
    const float tol = std::clamp(settings.ChromaTolerance, 0.02f, 1.20f);
    const float soft = std::clamp(settings.ChromaSoftness, 0.01f, 0.60f);

    if (activeMode != CutoutKeyMode::PreserveExistingAlpha) {
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

                // Combine color-space distance with green channel excess so dark ink & warm tones stay 100% opaque
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
        const size_t totalPixels = static_cast<size_t>(width) * height;
        std::vector<uint8_t> validRGBMask(totalPixels, 0u);
        for (size_t i = 0; i < totalPixels; ++i) {
            if (rgba[i * 4u + 3] >= alphaThreshold) {
                validRGBMask[i] = 1u;
            }
        }

        std::vector<uint8_t> prevRGBA = rgba;
        std::vector<uint8_t> nextMask = validRGBMask;

        for (uint32_t pass = 0; pass < settings.AlphaBleedPasses; ++pass) {
            prevRGBA = rgba;
            nextMask = validRGBMask;

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
                        // Preserve exact A == 0 (or original sub-threshold alpha) so silhouette never bloats
                        nextMask[center] = 1u;
                    }
                }
            }
            validRGBMask.swap(nextMask);
        }
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
    stbi_set_flip_vertically_on_load(0);
    int w = 0, h = 0, channels = 0;
    unsigned char* rawPixels = stbi_load(inStr.c_str(), &w, &h, &channels, 4);
    // Restore Engine::Texture's expected bottom-up OpenGL load convention immediately
    stbi_set_flip_vertically_on_load(1);

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

    stbi_flip_vertically_on_write(0);
    const int writeOk = stbi_write_png(outStr.c_str(),
                                       static_cast<int>(width),
                                       static_cast<int>(height),
                                       4,
                                       rgba.data(),
                                       static_cast<int>(width * 4u));
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

} // namespace Engine

