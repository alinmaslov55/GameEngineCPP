#ifndef UTILS_CUTOUTPROCESSOR_H
#define UTILS_CUTOUTPROCESSOR_H

#include "Engine/Core/JobHandle.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Engine {

enum class CutoutKeyMode {
    AutoDetectCorner = 0,      // Inspects 4 image corners for existing alpha, #00FF00 green, #FF00FF magenta, or solid backdrop
    ChromaGreen = 1,           // Keys out #00FF00 green screen + despills green edge halos
    ChromaMagenta = 2,         // Keys out #FF00FF magenta screen + despills magenta edge halos
    PreserveExistingAlpha = 3  // Keeps existing alpha channel; performs optional bottom-flush crop + RGB edge bleed
};

struct CutoutProcessSettings {
    CutoutKeyMode BackgroundMode = CutoutKeyMode::AutoDetectCorner;
    float ChromaTolerance = 0.34f;       // Normalized RGB distance below which background becomes fully transparent
    float ChromaSoftness = 0.14f;        // Smooth ramp width for anti-aliased outer ink outlines
    bool DespillFringe = true;           // Neutralizes green/magenta color spill on semi-transparent outline edges
    bool AutoTrimAndBottomFlush = true;  // Crops margins & aligns lowest opaque pixel to bottom row (UV.y = 0 ground pivot)
    uint8_t TrimAlphaThreshold = 24;     // Minimum alpha [1..255] treated as solid content during bounds detection
    uint32_t HorizontalPaddingPx = 16;   // Left/right breathing room (pixels)
    uint32_t TopPaddingPx = 20;          // Top breathing room above tallest pixel (pixels)
    bool SquareCanvas = false;           // If true, expands canvas to max(W, H) while keeping bottom-center ground pivot
    uint32_t AlphaBleedPasses = 4;       // Dilates opaque border RGB into A==0 neighbors to prevent trilinear mipmap halos
};

struct CutoutProcessResult {
    bool Success = false;
    uint32_t OutputWidth = 0;
    uint32_t OutputHeight = 0;
    uint32_t ContentWidth = 0;
    uint32_t ContentHeight = 0;
    float SuggestedAspectRatio = 1.0f;
    CutoutKeyMode ResolvedKeyMode = CutoutKeyMode::PreserveExistingAlpha;
    std::string ErrorMessage;
};

struct SheetCutoutSettings {
    CutoutProcessSettings BaseSettings;
    uint32_t Columns = 0;              // Output grid columns (0 = auto 1-row horizontal strip)
    uint32_t Rows = 0;                 // Slicing rows when input is a single sheet grid
    bool LockSharedUnionBounds = true; // Computes union [minX..maxX, minY..maxY] across ALL frames
    bool LockBottomFootPivot = true;   // Aligns union maxY to cell bottom (ground contact UV.y = 0)
    bool LockHorizontalCenter = true;  // Centers the union content horizontally within each cell
    float FrameDurationMs = 100.0f;    // Frame hold time in milliseconds for .atlas.json
    std::string FrameNamePrefix = "";  // Optional prefix for frame naming (e.g. "spr_hero_chop")
};

class CutoutProcessor {
public:
    // Processes an in-memory top-down RGBA8 image buffer (row 0 = top, row height-1 = bottom ground contact)
    static CutoutProcessResult ProcessRGBAInPlace(std::vector<uint8_t>& rgba,
                                                  uint32_t& width,
                                                  uint32_t& height,
                                                  const CutoutProcessSettings& settings = {});

    // Loads an image file (.png/.jpg/.bmp), runs the 2.5D cutout pipeline, and writes a lossless RGBA8 PNG
    static CutoutProcessResult ProcessFile(const std::filesystem::path& inputPath,
                                           const std::filesystem::path& outputPath,
                                           const CutoutProcessSettings& settings = {});

    // Asynchronously loads an image file, runs the 2.5D cutout pipeline on a worker thread,
    // and writes a lossless RGBA8 PNG to disk without blocking the main thread.
    static JobHandle ProcessFileAsync(const std::filesystem::path& inputPath,
                                      const std::filesystem::path& outputPath,
                                      const CutoutProcessSettings& settings = {},
                                      std::function<void(const CutoutProcessResult&)> onComplete = nullptr);

    // Normalizes a multi-frame animation sequence into a composite sprite sheet with locked union bounds
    // and bottom-flushed foot ground contact, exporting both sheet PNG and TexturePacker .atlas.json (ANI-03)
    static CutoutProcessResult ProcessAnimationStrip(
        const std::vector<std::filesystem::path>& inputFrameFiles,
        const std::filesystem::path& outputSheetPng,
        const std::filesystem::path& outputAtlasJson,
        const SheetCutoutSettings& settings = {});

    // Asynchronous worker-thread execution of ProcessAnimationStrip
    static JobHandle ProcessAnimationStripAsync(
        const std::vector<std::filesystem::path>& inputFrameFiles,
        const std::filesystem::path& outputSheetPng,
        const std::filesystem::path& outputAtlasJson,
        const SheetCutoutSettings& settings = {},
        std::function<void(const CutoutProcessResult&)> onComplete = nullptr);
};

} // namespace Engine

#endif // UTILS_CUTOUTPROCESSOR_H

